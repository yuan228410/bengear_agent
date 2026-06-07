#include "ben_gear/base/net/wakeup_fd.hpp"

#include <cerrno>

#if BEN_GEAR_PLATFORM_WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <unistd.h>
#include <fcntl.h>
#if BEN_GEAR_PLATFORM_LINUX
#include <sys/eventfd.h>
#endif
#endif

namespace ben_gear::net {

// ---------------------------------------------------------------------------
// Windows 实现：WSA socket pair + WSAEventSelect
// ---------------------------------------------------------------------------
#if BEN_GEAR_PLATFORM_WINDOWS

WakeupFd::WakeupFd() {
    // 创建 TCP loopback socket pair 作为唤醒机制
    // 1. 创建监听 socket
    SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        throw std::runtime_error("WakeupFd: socket() failed: " + std::to_string(WSAGetLastError()));
    }

    // 绑定到 loopback 临时端口
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;  // 系统分配端口
    if (::bind(listener, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        ::closesocket(listener);
        throw std::runtime_error("WakeupFd: bind() failed");
    }

    // 获取分配的端口
    int addr_len = sizeof(addr);
    if (::getsockname(listener, (struct sockaddr*)&addr, &addr_len) == SOCKET_ERROR) {
        ::closesocket(listener);
        throw std::runtime_error("WakeupFd: getsockname() failed");
    }

    if (::listen(listener, 1) == SOCKET_ERROR) {
        ::closesocket(listener);
        throw std::runtime_error("WakeupFd: listen() failed");
    }

    // 2. 创建写入端 socket 并连接
    write_sock_ = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (write_sock_ == INVALID_SOCKET) {
        ::closesocket(listener);
        throw std::runtime_error("WakeupFd: write socket() failed");
    }

    // 非阻塞连接
    u_long mode = 1;
    ::ioctlsocket(write_sock_, FIONBIO, &mode);

    if (::connect(write_sock_, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK) {
            ::closesocket(write_sock_);
            ::closesocket(listener);
            throw std::runtime_error("WakeupFd: connect() failed");
        }
    }

    // 3. 接受连接
    read_sock_ = ::accept(listener, nullptr, nullptr);
    if (read_sock_ == INVALID_SOCKET) {
        ::closesocket(write_sock_);
        ::closesocket(listener);
        throw std::runtime_error("WakeupFd: accept() failed");
    }

    ::closesocket(listener);  // 不再需要监听 socket

    // 设置读取端非阻塞
    mode = 1;
    ::ioctlsocket(read_sock_, FIONBIO, &mode);

    // 禁用 Nagle 算法，减少延迟
    int nodelay = 1;
    ::setsockopt(read_sock_, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, sizeof(nodelay));
    ::setsockopt(write_sock_, IPPROTO_TCP, TCP_NODELAY, (char*)&nodelay, sizeof(nodelay));

    // 创建 WSA 事件对象
    wsa_event_ = ::WSACreateEvent();
    if (wsa_event_ == WSA_INVALID_EVENT) {
        ::closesocket(read_sock_);
        ::closesocket(write_sock_);
        throw std::runtime_error("WakeupFd: WSACreateEvent() failed");
    }

    // 将读取端 socket 与 WSA 事件关联
    if (::WSAEventSelect(read_sock_, wsa_event_, FD_READ) == SOCKET_ERROR) {
        ::WSACloseEvent(wsa_event_);
        ::closesocket(read_sock_);
        ::closesocket(write_sock_);
        throw std::runtime_error("WakeupFd: WSAEventSelect() failed");
    }
}

WakeupFd::~WakeupFd() {
    if (wsa_event_ != WSA_INVALID_EVENT) {
        ::WSAEventSelect(read_sock_, WSA_INVALID_EVENT, 0);  // 解除关联
        ::WSACloseEvent(wsa_event_);
    }
    if (read_sock_ != INVALID_SOCKET) ::closesocket(read_sock_);
    if (write_sock_ != INVALID_SOCKET) ::closesocket(write_sock_);
}

void WakeupFd::notify() {
    if (write_sock_ != INVALID_SOCKET) {
        char c = 1;
        ::send(write_sock_, &c, 1, 0);
    }
}

void WakeupFd::drain() {
    if (read_sock_ != INVALID_SOCKET) {
        char buf[64];
        while (::recv(read_sock_, buf, sizeof(buf), 0) > 0) {}
    }
}

int WakeupFd::read_fd() const {
    // Windows: WSA 不使用 fd，返回 -1
    // EventLoop 的 Windows 实现需要使用 WSAEventWait 而非 epoll/kqueue
    return -1;
}

bool WakeupFd::valid() const {
    return read_sock_ != INVALID_SOCKET && write_sock_ != INVALID_SOCKET;
}

// ---------------------------------------------------------------------------
// Linux / macOS 实现
// ---------------------------------------------------------------------------
#else

WakeupFd::WakeupFd() {
#if BEN_GEAR_PLATFORM_LINUX
    fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (fd_ < 0) {
        throw std::runtime_error("eventfd failed");
    }
#else
    if (pipe(pipe_) != 0) {
        throw std::runtime_error("pipe failed");
    }
    fcntl(pipe_[0], F_SETFL, O_NONBLOCK);
    fcntl(pipe_[1], F_SETFL, O_NONBLOCK);
#endif
}

WakeupFd::~WakeupFd() {
#if BEN_GEAR_PLATFORM_LINUX
    if (fd_ >= 0) ::close(fd_);
#else
    if (pipe_[0] >= 0) ::close(pipe_[0]);
    if (pipe_[1] >= 0) ::close(pipe_[1]);
#endif
}

void WakeupFd::notify() {
#if BEN_GEAR_PLATFORM_LINUX
    if (fd_ >= 0) {
        uint64_t val = 1;
        while (::write(fd_, &val, sizeof(val)) < 0 && errno == EINTR) {}
    }
#else
    if (pipe_[1] >= 0) {
        char c = 1;
        while (::write(pipe_[1], &c, 1) < 0 && errno == EINTR) {}
    }
#endif
}

void WakeupFd::drain() {
#if BEN_GEAR_PLATFORM_LINUX
    if (fd_ >= 0) {
        uint64_t val;
        while (::read(fd_, &val, sizeof(val)) == sizeof(val)) {}
    }
#else
    if (pipe_[0] >= 0) {
        char buf[64];
        while (::read(pipe_[0], buf, sizeof(buf)) > 0) {}
    }
#endif
}

int WakeupFd::read_fd() const {
#if BEN_GEAR_PLATFORM_LINUX
    return fd_;
#else
    return pipe_[0];
#endif
}

bool WakeupFd::valid() const {
#if BEN_GEAR_PLATFORM_LINUX
    return fd_ >= 0;
#else
    return pipe_[0] >= 0 && pipe_[1] >= 0;
#endif
}

#endif  // BEN_GEAR_PLATFORM_WINDOWS

}  // namespace ben_gear::net