#include "webserver/socket.hpp"
#include "webserver/logging.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>
#include <system_error>

namespace ws {

// ============ TCPServerSocket ============

TCPServerSocket::TCPServerSocket(const std::string& host, uint16_t port, int backlog)
    : fd_(-1)
    , host_(host)
    , port_(port)
    , backlog_(backlog)
    , reuse_addr_(true)
    , reuse_port_(false)
    , tcp_no_delay_(true) {

    log::info_fmt("TCPServerSocket created: {}:{}", host, port);
}

TCPServerSocket::~TCPServerSocket() {
    close();
}

bool TCPServerSocket::bind_and_listen() {
    // 创建 socket
    fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ == -1) {
        log::error_fmt("socket() failed: {}", ::strerror(errno));
        return false;
    }

    // 设置 SO_REUSEADDR
    if (reuse_addr_) {
        int opt = 1;
        if (::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
            log::error_fmt("setsockopt(SO_REUSEADDR) failed: {}", ::strerror(errno));
            close();
            return false;
        }
    }

    // 设置 SO_REUSEPORT (macOS 支持)
    if (reuse_port_) {
        int opt = 1;
        if (::setsockopt(fd_, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)) == -1) {
            log::error_fmt("setsockopt(SO_REUSEPORT) failed: {}", ::strerror(errno));
            close();
            return false;
        }
    }

    // 设置 TCP_NODELAY
    if (tcp_no_delay_) {
        int opt = 1;
        if (::setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt)) == -1) {
            log::error_fmt("setsockopt(TCP_NODELAY) failed: {}", ::strerror(errno));
            close();
            return false;
        }
    }

    // 设置非阻塞
    if (!set_non_blocking(fd_)) {
        log::error_fmt("Failed to set non-blocking: {}", ::strerror(errno));
        close();
        return false;
    }

    // bind
    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);

    if (host_.empty() || host_ == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (::inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) != 1) {
            log::error_fmt("inet_pton() failed: invalid address {}", host_);
            close();
            return false;
        }
    }

    if (::bind(fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == -1) {
        log::error_fmt("bind({}:{}) failed: {}", host_, port_, ::strerror(errno));
        close();
        return false;
    }

    // listen
    if (::listen(fd_, backlog_) == -1) {
        log::error_fmt("listen() failed: {}", ::strerror(errno));
        close();
        return false;
    }

    log::info_fmt("TCPServerSocket listening on {}:{} (fd={})", host_, port_, fd_);
    return true;
}

int TCPServerSocket::accept() {
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    int client_fd = ::accept(fd_, reinterpret_cast<struct sockaddr*>(&client_addr), &addr_len);

    if (client_fd == -1) {
        // EAGAIN/EWOULDBLOCK 是正常的（非阻塞模式无连接待处理）
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            log::error_fmt("accept() failed: {}", ::strerror(errno));
        }
        return -1;
    }

    // 设置非阻塞
    if (!set_non_blocking(client_fd)) {
        log::error_fmt("Failed to set client fd non-blocking: {}", ::strerror(errno));
        ::close(client_fd);
        return -1;
    }

    // 设置 TCP_NODELAY
    if (tcp_no_delay_) {
        int opt = 1;
        ::setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));
    }

    char client_ip[INET_ADDRSTRLEN];
    ::inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
    int client_port = ntohs(client_addr.sin_port);

    log::debug_fmt("Accepted connection: fd={}, client={}:{}", client_fd, client_ip, client_port);

    return client_fd;
}

void TCPServerSocket::close() {
    if (fd_ != -1) {
        ::close(fd_);
        log::info_fmt("TCPServerSocket closed: fd={}", fd_);
        fd_ = -1;
    }
}

int TCPServerSocket::fd() const {
    return fd_;
}

// ============ 辅助函数 ============

bool set_non_blocking(int fd) {
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags == -1) return false;
    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1;
}

ssize_t read_full(int fd, char* buffer, size_t size) {
    ssize_t total_read = 0;
    while (total_read < static_cast<ssize_t>(size)) {
        ssize_t n = ::read(fd, buffer + total_read, size - total_read);
        if (n > 0) {
            total_read += n;
        } else if (n == 0) {
            // EOF
            break;
        } else {
            // EAGAIN/EWOULDBLOCK 不是错误
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            return -1;  // 真正的错误
        }
    }
    return total_read;
}

ssize_t write_full(int fd, const char* buffer, size_t size) {
    ssize_t total_written = 0;
    while (total_written < static_cast<ssize_t>(size)) {
        ssize_t n = ::write(fd, buffer + total_written, size - total_written);
        if (n > 0) {
            total_written += n;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;  // 非阻塞，下次再写
            }
            return -1;
        }
    }
    return total_written;
}

}  // namespace ws
