#include "ben_gear/base/net/tcp_stream.hpp"

#include <algorithm>
#include <cerrno>
#ifndef _WIN32
#include <sys/socket.h>
#endif
#include <stdexcept>

namespace ben_gear::net {

namespace {

int send_flags() noexcept {
#ifdef MSG_NOSIGNAL
    return MSG_NOSIGNAL;
#else
    return 0;
#endif
}

}  // namespace

Task<std::size_t> TcpStream::read_some(char* data, std::size_t size) {
#ifdef _WIN32
    // Windows: 使用 IOCP 完成模式（WSARecv），避免 select 回退路径
    co_return co_await loop_->read_some(socket_.get(), data, size);
#else
    for (;;) {
        if (!socket_.valid()) {
            throw std::runtime_error("recv failed: socket closed by response timeout");
        }
        const auto received = socket_recv(socket_.get(), data, size, 0);
        if (received > 0) {
            co_return static_cast<std::size_t>(received);
        }
        if (received == 0) {
            co_return 0;
        }
        if (!would_block()) {
            throw std::runtime_error("recv failed: " + last_socket_error());
        }
        co_await loop_->wait_read(socket_.get());
    }
#endif
}

Task<std::size_t> TcpStream::write_some(const char* data, std::size_t size) {
#ifdef _WIN32
    // Windows: 使用 IOCP 完成模式（WSASend），避免 select 回退路径
    co_return co_await loop_->write_some(socket_.get(), data, size);
#else
    for (;;) {
        const auto sent = socket_send(socket_.get(), data, size, send_flags());
        if (sent >= 0) {
            co_return static_cast<std::size_t>(sent);
        }
        if (!would_block()) {
            throw std::runtime_error("send failed: " + last_socket_error());
        }
        co_await loop_->wait_write(socket_.get());
    }
#endif
}

Task<void> TcpStream::write_all(std::string_view data) {
#ifdef _WIN32
    // Windows: 使用 IOCP write_some，循环确保写入全部数据
    std::size_t written = 0;
    while (written < data.size()) {
        written += co_await write_some(data.data() + written, data.size() - written);
    }
#else
    std::size_t written = 0;
    while (written < data.size()) {
        const auto sent = socket_send(socket_.get(),
            data.data() + written, data.size() - written, send_flags());
        if (sent > 0) {
            written += static_cast<std::size_t>(sent);
            continue;
        }
        if (sent == 0) {
            throw std::runtime_error("send returned zero bytes");
        }
        if (!would_block()) {
            throw std::runtime_error("send failed: " + last_socket_error());
        }
        co_await loop_->wait_write(socket_.get());
    }
#endif
}

Task<void> TcpStream::read_all(char* data, std::size_t size) {
#ifdef _WIN32
    // Windows: 使用 IOCP read_some，循环确保读取全部数据
    std::size_t total = 0;
    while (total < size) {
        auto n = co_await read_some(data + total, size - total);
        if (n == 0) {
            throw std::runtime_error("read_all failed: connection closed ("
                + std::to_string(total) + "/" + std::to_string(size) + " bytes)");
        }
        total += n;
    }
#else
    std::size_t total = 0;
    while (total < size) {
        if (!socket_.valid()) {
            throw std::runtime_error("read_all failed: socket closed");
        }
        const auto received = socket_recv(socket_.get(), data + total, size - total, 0);
        if (received > 0) {
            total += static_cast<std::size_t>(received);
            continue;
        }
        if (received == 0) {
            throw std::runtime_error("read_all failed: connection closed ("
                + std::to_string(total) + "/" + std::to_string(size) + " bytes)");
        }
        if (!would_block()) {
            throw std::runtime_error("read_all recv failed: " + last_socket_error());
        }
        co_await loop_->wait_read(socket_.get());
    }
#endif
}

Task<TcpStream> async_connect(EventLoop& loop, std::string host, std::string port,
                              std::chrono::milliseconds timeout) {
    auto socket = connect_tcp_non_blocking(host, port);

    // Register a close-after timeout: if the fd isn't cancelled before
    // the deadline, the EventLoop will close it, causing wait_write
    // to return with an error.  This is a lightweight fd-deadline map
    // — no extra coroutine or TimerOperation per connection.
    auto raw_fd = socket.get();
    loop.close_after(raw_fd, timeout);

    co_await loop.wait_write(socket.get());

    // Cancel the timeout — connection attempt finished (success or failure).
    loop.cancel_close(raw_fd);

    // Check if the connection actually succeeded
    int error = 0;
    socklen_t len = sizeof(error);
#ifdef _WIN32
    getsockopt(socket.get(), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &len);
#else
    getsockopt(socket.get(), SOL_SOCKET, SO_ERROR, &error, &len);
#endif
    if (error != 0) {
        throw std::runtime_error("async connect failed: " + std::to_string(error));
    }
    if (!socket.valid()) {
        throw std::runtime_error("async connect timed out after " +
            std::to_string(timeout.count()) + "ms to " + host + ":" + port);
    }

    co_return TcpStream(loop, std::move(socket));
}

}  // namespace ben_gear::net
