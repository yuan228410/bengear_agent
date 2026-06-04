#include "ben_gear/base/net/socket.hpp"

#include <cerrno>
#include <cstring>
#include <system_error>

namespace ben_gear::net {

NetworkRuntime::NetworkRuntime() {
#ifdef _WIN32
    if (WSAStartup(MAKEWORD(2, 2), &data_) != 0) {
        throw std::runtime_error("WSAStartup failed");
    }
#endif
}

NetworkRuntime::~NetworkRuntime() {
#ifdef _WIN32
    WSACleanup();
#endif
}

void close_socket(socket_handle socket) noexcept {
#ifdef _WIN32
    closesocket(socket);
#else
    ::close(socket);
#endif
}

void set_non_blocking(socket_handle socket) {
#ifdef _WIN32
    u_long mode = 1;
    if (ioctlsocket(socket, FIONBIO, &mode) != 0) {
        throw std::runtime_error(last_socket_error());
    }
#else
    const int flags = fcntl(socket, F_GETFL, 0);
    if (flags < 0 || fcntl(socket, F_SETFL, flags | O_NONBLOCK) < 0) {
        throw std::runtime_error(last_socket_error());
    }
#endif
}

bool would_block() noexcept {
#ifdef _WIN32
    const auto error = WSAGetLastError();
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS;
#endif
}

std::string last_socket_error() {
#ifdef _WIN32
    return "socket error: " + std::to_string(WSAGetLastError());
#else
    return std::strerror(errno);
#endif
}

bool send_tcp_message(std::string_view host, std::string_view port, std::string_view message) noexcept {
    try {
        NetworkRuntime runtime;
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;

        addrinfo* result = nullptr;
        if (getaddrinfo(std::string(host).c_str(), std::string(port).c_str(), &hints, &result) != 0) {
            return false;
        }

        Socket socket;
        for (auto* item = result; item != nullptr; item = item->ai_next) {
            socket_handle handle = ::socket(item->ai_family, item->ai_socktype, item->ai_protocol);
            if (handle == invalid_socket_handle) {
                continue;
            }
            if (::connect(handle, item->ai_addr, static_cast<int>(item->ai_addrlen)) == 0) {
                socket.reset(handle);
                break;
            }
            close_socket(handle);
        }
        freeaddrinfo(result);
        if (!socket.valid()) {
            return false;
        }

        const std::string payload(message);
        std::size_t written = 0;
        while (written < payload.size()) {
            const auto sent = socket_send(socket.get(),
                payload.data() + written, payload.size() - written, 0);
            if (sent <= 0) {
                return false;
            }
            written += static_cast<std::size_t>(sent);
        }
        return true;
    } catch (...) {
        return false;
    }
}

Socket connect_tcp_non_blocking(std::string_view host, std::string_view port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* result = nullptr;
    const auto lookup = getaddrinfo(std::string(host).c_str(), std::string(port).c_str(), &hints, &result);
    if (lookup != 0) {
#ifdef _WIN32
        throw std::runtime_error("getaddrinfo failed: " + std::to_string(lookup));
#else
        throw std::runtime_error(gai_strerror(lookup));
#endif
    }

    Socket socket;
    for (auto* item = result; item != nullptr; item = item->ai_next) {
        socket_handle handle = ::socket(item->ai_family, item->ai_socktype, item->ai_protocol);
        if (handle == invalid_socket_handle) {
            continue;
        }

        try {
            set_non_blocking(handle);
        } catch (...) {
            close_socket(handle);
            continue;
        }

        const int connected = ::connect(handle, item->ai_addr, static_cast<int>(item->ai_addrlen));
        if (connected == 0 || would_block()) {
            socket.reset(handle);
            break;
        }
        close_socket(handle);
    }
    freeaddrinfo(result);

    if (!socket.valid()) {
        throw std::runtime_error("connect failed: " + last_socket_error());
    }
    return socket;
}

Socket tcp_listen(std::string_view host, int port, int backlog) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    addrinfo* result = nullptr;
    const auto lookup = getaddrinfo(
        host.empty() ? nullptr : std::string(host).c_str(),
        std::to_string(port).c_str(), &hints, &result);
    if (lookup != 0) {
#ifdef _WIN32
        throw std::runtime_error("tcp_listen getaddrinfo failed: " + std::to_string(lookup));
#else
        throw std::runtime_error(std::string("tcp_listen getaddrinfo failed: ") + gai_strerror(lookup));
#endif
    }

    Socket socket;
    for (auto* item = result; item != nullptr; item = item->ai_next) {
        socket_handle handle = ::socket(item->ai_family, item->ai_socktype, item->ai_protocol);
        if (handle == invalid_socket_handle) {
            continue;
        }

        setsockopt_int(handle, SOL_SOCKET, SO_REUSEADDR, 1);

        if (::bind(handle, item->ai_addr, static_cast<int>(item->ai_addrlen)) == 0 &&
            ::listen(handle, backlog) == 0) {
            socket.reset(handle);
            break;
        }
        close_socket(handle);
    }
    freeaddrinfo(result);

    if (!socket.valid()) {
        throw std::runtime_error("tcp_listen failed: " + last_socket_error());
    }
    return socket;
}

Socket tcp_accept(socket_handle listen_fd) {
    auto client = ::accept(listen_fd, nullptr, nullptr);
    return Socket(client);
}

Socket udp_bind(std::string_view host, int port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;
    hints.ai_flags = AI_PASSIVE;

    addrinfo* result = nullptr;
    const auto lookup = getaddrinfo(
        host.empty() ? nullptr : std::string(host).c_str(),
        std::to_string(port).c_str(), &hints, &result);
    if (lookup != 0) {
#ifdef _WIN32
        throw std::runtime_error("udp_bind getaddrinfo failed: " + std::to_string(lookup));
#else
        throw std::runtime_error(std::string("udp_bind getaddrinfo failed: ") + gai_strerror(lookup));
#endif
    }

    Socket socket;
    for (auto* item = result; item != nullptr; item = item->ai_next) {
        socket_handle handle = ::socket(item->ai_family, item->ai_socktype, item->ai_protocol);
        if (handle == invalid_socket_handle) {
            continue;
        }

        setsockopt_int(handle, SOL_SOCKET, SO_REUSEADDR, 1);

        if (::bind(handle, item->ai_addr, static_cast<int>(item->ai_addrlen)) == 0) {
            socket.reset(handle);
            break;
        }
        close_socket(handle);
    }
    freeaddrinfo(result);

    if (!socket.valid()) {
        throw std::runtime_error("udp_bind failed: " + last_socket_error());
    }
    return socket;
}

bool udp_send_to(socket_handle fd, std::string_view host, int port, std::string_view data) noexcept {
    try {
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = IPPROTO_UDP;

        addrinfo* result = nullptr;
        if (getaddrinfo(std::string(host).c_str(), std::to_string(port).c_str(), &hints, &result) != 0) {
            return false;
        }

        std::size_t sent_total = 0;
        for (auto* item = result; item != nullptr && sent_total == 0; item = item->ai_next) {
            const auto sent = socket_sendto(fd,
                data.data(), data.size(), 0,
                item->ai_addr, static_cast<socklen_t>(item->ai_addrlen));
            if (sent > 0) {
                sent_total = static_cast<std::size_t>(sent);
            }
        }
        freeaddrinfo(result);
        return sent_total > 0;
    } catch (...) {
        return false;
    }
}

}  // namespace ben_gear::net
