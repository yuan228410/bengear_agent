// ==========================================================================
// tcp_echo_server.cpp — Linux epoll-based TCP echo server
// ==========================================================================
//
// Architecture (single-threaded event-driven, epoll ET):
//
//   ┌──────────────────────────────────────────────────────┐
//   │  main()                                               │
//   │   ├─ NetworkRuntime (RAII init)                       │
//   │   ├─ IoContext ctx("tcp-server")   ← owns EventLoop   │
//   │   └─ sync_wait → serve(loop, port)                    │
//   └──────────────────────────────────────────────────────┘
//                         │
//   ┌─────────────────────▼────────────────────────────────┐
//   │  serve() coroutine                                     │
//   │   ├─ tcp_listen(port) → non-blocking listen fd         │
//   │   ├─ loop.wait_read(listen_fd)   ← EPOLLIN | ONESHOT   │
//   │   ├─ accept() → new connection fd                      │
//   │   ├─ spawn handle_client(fd)                           │
//   │   └─ loop back to wait_read                            │
//   └──────────────────────────────────────────────────────┘
//                         │
//   ┌─────────────────────▼────────────────────────────────┐
//   │  handle_client(fd) coroutine                           │
//   │   ┌─ Connection state:                                 │
//   │   │   - read_buf:   recv buffer (4KB)                  │
//   │   │   - write_buf:  send queue                         │
//   │   │   - FrameParser: length-prefix protocol parser     │
//   │   ├─ loop:                                             │
//   │   │   ├─ wait_read(fd) → recv() until EAGAIN (ET!)     │
//   │   │   ├─ parser.feed(data) → vector<Message>           │
//   │   │   ├─ for each Message: echo back (write_buf)       │
//   │   │   └─ if write_buf non-empty: wait_write(fd) → send │
//   │   └─ close on EOF / error                              │
//   └──────────────────────────────────────────────────────┘
//
// Protocol frame format (分隔符+长度前缀):
//   ┌──────────────┬──────────────────┐
//   │  4 bytes BE  │   N bytes        │
//   │  payload len │   payload        │
//   └──────────────┴──────────────────┘
//
// Key design decisions:
//   - 单线程事件循环 (no mutex needed for connection state)
//   - 边缘触发 ET via EPOLLONESHOT (high performance, careful loop-read)
//   - 长度前缀协议 (simple, robust against粘包/半包)
//
// ==========================================================================

#include "net/event_loop.hpp"
#include "net/io_context.hpp"
#include "net/socket.hpp"
#include "net/task.hpp"
#include "log/logger.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#ifdef __linux__
#include <netinet/tcp.h>  // TCP_NODELAY
#endif

using namespace ben_gear::net;
namespace lg = ben_gear::log;

// ==========================================================================
// Protocol: 4-byte big-endian length prefix + payload
// ==========================================================================
class FrameParser {
public:
    /// Feed received bytes into the parser.
    /// @return vector of complete messages extracted from the byte stream.
    std::vector<std::string> feed(const char* data, size_t len) {
        buffer_.insert(buffer_.end(), data, data + len);
        return extract_frames();
    }

    /// Reset parser state for a new connection.
    void reset() {
        buffer_.clear();
        header_read_ = false;
        expected_len_ = 0;
    }

private:
    std::vector<std::string> extract_frames() {
        std::vector<std::string> frames;
        while (true) {
            if (!header_read_) {
                if (buffer_.size() < kHeaderSize) break;
                expected_len_ = read_be32(buffer_.data());
                buffer_.erase(buffer_.begin(), buffer_.begin() + kHeaderSize);
                header_read_ = true;
                // Sanity check: reject unreasonable lengths
                if (expected_len_ > kMaxPayloadSize) {
                    lg::warn_fmt("FrameParser: payload too large ({}), resetting", expected_len_);
                    reset();
                    break;
                }
            }
            if (header_read_) {
                if (buffer_.size() < expected_len_) break;
                frames.emplace_back(reinterpret_cast<const char*>(buffer_.data()), expected_len_);
                buffer_.erase(buffer_.begin(), buffer_.begin() + expected_len_);
                header_read_ = false;
                expected_len_ = 0;
            }
        }
        return frames;
    }

    static uint32_t read_be32(const uint8_t* p) {
        return (static_cast<uint32_t>(p[0]) << 24) |
               (static_cast<uint32_t>(p[1]) << 16) |
               (static_cast<uint32_t>(p[2]) << 8)  |
               (static_cast<uint32_t>(p[3]));
    }

    static constexpr size_t kHeaderSize = 4;
    static constexpr size_t kMaxPayloadSize = 16 * 1024 * 1024;  // 16 MB sanity limit

    std::vector<uint8_t> buffer_;
    bool header_read_ = false;
    size_t expected_len_ = 0;
};

// ==========================================================================
// Write a length-prefixed frame: [4-byte BE len][payload]
// ==========================================================================
static std::string encode_frame(std::string_view payload) {
    std::string frame;
    uint32_t len = static_cast<uint32_t>(payload.size());
    frame.reserve(4 + len);
    frame.push_back(static_cast<char>((len >> 24) & 0xFF));
    frame.push_back(static_cast<char>((len >> 16) & 0xFF));
    frame.push_back(static_cast<char>((len >> 8) & 0xFF));
    frame.push_back(static_cast<char>(len & 0xFF));
    frame.append(payload);
    return frame;
}

// ==========================================================================
// Connection handler — one coroutine per client
// ==========================================================================
static Task<void> handle_client(EventLoop& loop, Socket client_sock) {
    const auto fd = client_sock.get();
    lg::info_fmt("Connection accepted: fd={}", static_cast<int>(fd));

    FrameParser parser;
    std::string write_buf;       // pending send data
    size_t write_offset = 0;     // bytes already sent from write_buf

    std::array<char, 4096> read_buf{};

    try {
        while (true) {
            // --- Read: ET模式必须循环读直到EAGAIN ---
            co_await loop.wait_read(fd);
            bool eof = false;
            while (true) {
                auto n = socket_recv(fd, read_buf.data(), read_buf.size(), 0);
                if (n > 0) {
                    auto messages = parser.feed(read_buf.data(), static_cast<size_t>(n));
                    for (auto& msg : messages) {
                        lg::info_fmt("Received message ({} bytes) from fd={}", msg.size(), static_cast<int>(fd));
                        // Echo: same frame back
                        write_buf += encode_frame(msg);
                    }
                } else if (n == 0) {
                    // EOF — client closed connection
                    lg::info_fmt("Client fd={} closed (EOF)", static_cast<int>(fd));
                    eof = true;
                    break;
                } else {
                    // n < 0
                    if (would_block()) {
                        break;  // No more data, exit read loop
                    }
                    // Real error
                    lg::warn_fmt("recv error on fd={}: {}", static_cast<int>(fd), last_socket_error());
                    eof = true;
                    break;
                }
            }
            if (eof) break;

            // --- Write: flush write buffer ---
            while (write_offset < write_buf.size()) {
                co_await loop.wait_write(fd);
                while (write_offset < write_buf.size()) {
                    auto n = socket_send(fd,
                        write_buf.data() + write_offset,
                        write_buf.size() - write_offset, 0);
                    if (n > 0) {
                        write_offset += static_cast<size_t>(n);
                    } else if (n < 0 && would_block()) {
                        break;  // Socket buffer full, re-wait
                    } else {
                        lg::warn_fmt("send error on fd={}: {}", static_cast<int>(fd), last_socket_error());
                        eof = true;
                        break;
                    }
                }
                if (eof) break;
            }
            if (eof) break;

            // Clear write buffer if fully sent
            if (write_offset >= write_buf.size()) {
                write_buf.clear();
                write_offset = 0;
            }
        }
    } catch (const std::exception& e) {
        lg::warn_fmt("Client fd={} exception: {}", static_cast<int>(fd), e.what());
    }

    // Cleanup
    lg::info_fmt("Connection closed: fd={}", static_cast<int>(fd));
    close_socket(fd);
    loop.on_socket_closed(fd);
    co_return;
}

// ==========================================================================
// Server coroutine — accept loop
// ==========================================================================
static Task<void> serve(EventLoop& loop, int port) {
    // 1. Create non-blocking listening socket
    auto listen_sock = tcp_listen("0.0.0.0", port, 128);
    auto listen_fd = listen_sock.get();
    set_non_blocking(listen_fd);

    lg::info_fmt("TCP echo server listening on port {} (fd={})", port, static_cast<int>(listen_fd));
    std::cout << "TCP echo server listening on port " << port
              << " (epoll ET, single-threaded, length-prefix protocol)" << std::endl;
    std::cout << "Connect with: nc localhost " << port << std::endl;
    std::cout << "Send framed messages: [4-byte BE len][payload]" << std::endl;

    // 2. Accept loop
    while (true) {
        co_await loop.wait_read(listen_fd);

        // ET mode: loop accept until EAGAIN
        while (true) {
            auto client = tcp_accept(listen_fd);
            if (!client.valid()) {
                if (would_block()) break;
                lg::error_fmt("accept error: {}", last_socket_error());
                break;
            }

            auto client_fd = client.get();
            set_non_blocking(client_fd);
#ifdef __linux__
            // Set TCP_NODELAY for low-latency echo
            setsockopt_int(client_fd, IPPROTO_TCP, TCP_NODELAY, 1);
#endif

            // Spawn client handler as fire-and-forget coroutine
            fire_and_forget(loop, handle_client(loop, std::move(client)));
        }
    }

    co_return;
}

// ==========================================================================
// main
// ==========================================================================
int main(int argc, char* argv[]) {
    int port = 9999;
    if (argc > 1) {
        port = std::stoi(argv[1]);
    }

    NetworkRuntime runtime;
    IoContext io("tcp-echo-server");

    std::cout << "Starting TCP echo server..." << std::endl;
    std::cout << "Architecture: single-threaded event loop, epoll ET (EPOLLONESHOT)" << std::endl;
    std::cout << "Protocol: 4-byte big-endian length prefix + payload" << std::endl;
    std::cout << "Press Ctrl+C to stop" << std::endl;

    try {
        sync_wait(io.loop(), serve(io.loop(), port));
    } catch (const std::exception& e) {
        lg::error_fmt("Server error: {}", e.what());
        std::cerr << "Server error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
