#include "webserver/tcp_server.hpp"
#include "webserver/logging.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace ws {

namespace {

// 设置非阻塞 socket
bool set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        log::error_fmt("fcntl F_GETFL failed: {}", strerror(errno));
        return false;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        log::error_fmt("fcntl F_SETFL O_NONBLOCK failed: {}", strerror(errno));
        return false;
    }
    return true;
}

// 启用 TCP_NODELAY 禁用 Nagle 算法
bool set_tcp_nodelay(int fd) {
    int flag = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) < 0) {
        log::error_fmt("setsockopt TCP_NODELAY failed: {}", strerror(errno));
        return false;
    }
    return true;
}

}  // anonymous namespace

// ============ TcpServer ============

TcpServer::TcpServer(const Config& config)
    : config_(config)
    , listen_fd_(-1)
    , is_running_(false)
{}

TcpServer::~TcpServer() {
    stop();
}

bool TcpServer::start() {
    if (is_running_) {
        log::info_fmt("TcpServer is already running");
        return true;
    }

    // 创建 socket
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ < 0) {
        log::error_fmt("Failed to create socket: {}", strerror(errno));
        return false;
    }

    // 设置 SO_REUSEADDR — 允许快速重启
    int optval = 1;
    if (setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR,
                   &optval, sizeof(optval)) < 0) {
        log::error_fmt("setsockopt SO_REUSEADDR failed: {}", strerror(errno));
        close(listen_fd_);
        return false;
    }

    // macOS 上 SO_REUSEPORT 允许多进程/线程绑定同一端口
#ifdef SO_REUSEPORT
    if (setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEPORT,
                   &optval, sizeof(optval)) < 0) {
        log::error_fmt("setsockopt SO_REUSEPORT failed: {}", strerror(errno));
        close(listen_fd_);
        return false;
    }
#endif

    // 设置为非阻塞
    if (!set_nonblocking(listen_fd_)) {
        close(listen_fd_);
        return false;
    }

    // 绑定地址
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config_.port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);  // 监听所有接口

    if (bind(listen_fd_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        log::error_fmt("Failed to bind to port {}: {}",
                       config_.port, strerror(errno));
        close(listen_fd_);
        return false;
    }

    // 开始监听
    if (listen(listen_fd_, config_.backlog) < 0) {
        log::error_fmt("Failed to listen on port {}: {}",
                       config_.port, strerror(errno));
        close(listen_fd_);
        return false;
    }

    is_running_ = true;
    log::info_fmt("TcpServer started on port {}, backlog={}",
                  config_.port, config_.backlog);

    return true;
}

void TcpServer::stop() {
    if (!is_running_) return;

    is_running_ = false;

    if (listen_fd_ >= 0) {
        close(listen_fd_);
        listen_fd_ = -1;
        log::info_fmt("TcpServer stopped");
    }
}

std::optional<TcpConnection> TcpServer::accept() {
    if (!is_running_) {
        return std::nullopt;
    }

    struct sockaddr_in client_addr{};
    socklen_t addr_len = sizeof(client_addr);

    int client_fd = ::accept(listen_fd_,
                              (struct sockaddr*)&client_addr,
                              &addr_len);

    if (client_fd < 0) {
        // EAGAIN/EWOULDBLOCK 是正常情况（无新连接）
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            log::error_fmt("accept failed: {}", strerror(errno));
        }
        return std::nullopt;
    }

    // 设置客户端 socket 为非阻塞
    if (!set_nonblocking(client_fd)) {
        close(client_fd);
        return std::nullopt;
    }

    // 设置 TCP_NODELAY
    set_tcp_nodelay(client_fd);

    // 获取客户端地址
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
    int client_port = ntohs(client_addr.sin_port);

    log::debug_fmt("New connection from {}:{} (fd={})",
                   client_ip, client_port, client_fd);

    TcpConnection conn{};
    conn.fd = client_fd;
    conn.client_ip = client_ip;
    conn.client_port = client_port;
    conn.connected_at = std::time(nullptr);

    return conn;
}

int TcpServer::listen_fd() const {
    return listen_fd_;
}

bool TcpServer::is_running() const {
    return is_running_;
}

const TcpServer::Config& TcpServer::config() const {
    return config_;
}

}  // namespace ws
