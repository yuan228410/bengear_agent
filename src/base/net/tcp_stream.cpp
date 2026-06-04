#include "ben_gear/base/net/tcp_stream.hpp"

#include <algorithm>
#include <cerrno>
#include <stdexcept>

namespace ben_gear::net {

Task<std::size_t> TcpStream::read_some(char* data, std::size_t size) {
    for (;;) {
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
}

Task<std::size_t> TcpStream::write_some(const char* data, std::size_t size) {
    for (;;) {
        const auto sent = socket_send(socket_.get(), data, size, 0);
        if (sent >= 0) {
            co_return static_cast<std::size_t>(sent);
        }
        if (!would_block()) {
            throw std::runtime_error("send failed: " + last_socket_error());
        }
        co_await loop_->wait_write(socket_.get());
    }
}

Task<void> TcpStream::write_all(std::string_view data) {
    std::size_t written = 0;
    while (written < data.size()) {
        const auto sent = socket_send(socket_.get(),
            data.data() + written, data.size() - written, 0);
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
}

Task<TcpStream> async_connect(EventLoop& loop, std::string host, std::string port) {
    auto socket = connect_tcp_non_blocking(host, port);
    co_await loop.wait_write(socket.get());
    co_return TcpStream(loop, std::move(socket));
}

}  // namespace ben_gear::net
