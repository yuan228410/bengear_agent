#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

// 跨平台 socket 头文件
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <WinSock2.h>
#include <WS2tcpip.h>
#else
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace ben_gear::net {

/// Socket 句柄类型（跨平台）
#ifdef _WIN32
using socket_handle = SOCKET;
inline constexpr socket_handle invalid_socket_handle = INVALID_SOCKET;
#else
using socket_handle = int;
inline constexpr socket_handle invalid_socket_handle = -1;
#endif

// ── Socket 跨平台兼容层 ─────────────────────────────────────
// 统一 send/recv/setsockopt 的平台差异，其他文件不再直接 #ifdef

/// send() — 统一 Windows int 强转
inline int socket_send(socket_handle fd, const void* buf, size_t len, int flags) {
#ifdef _WIN32
    return ::send(fd, static_cast<const char*>(buf), static_cast<int>(len), flags);
#else
    return ::send(fd, buf, len, flags);
#endif
}

/// recv() — 统一 Windows int 强转
inline int socket_recv(socket_handle fd, void* buf, size_t len, int flags) {
#ifdef _WIN32
    return ::recv(fd, static_cast<char*>(buf), static_cast<int>(len), flags);
#else
    return ::recv(fd, buf, len, flags);
#endif
}

/// sendto() — 统一 Windows int 强转
inline int socket_sendto(socket_handle fd, const void* buf, size_t len, int flags,
                         const struct sockaddr* dest_addr, socklen_t addrlen) {
#ifdef _WIN32
    return ::sendto(fd, static_cast<const char*>(buf), static_cast<int>(len),
                    flags, dest_addr, static_cast<int>(addrlen));
#else
    return ::sendto(fd, buf, len, flags, dest_addr, addrlen);
#endif
}

/// setsockopt() — 统一 Windows const char* 强转
inline int setsockopt_int(socket_handle fd, int level, int optname, int value) {
#ifdef _WIN32
    return ::setsockopt(fd, level, optname,
                        reinterpret_cast<const char*>(&value), sizeof(value));
#else
    return ::setsockopt(fd, level, optname, &value, sizeof(value));
#endif
}

/// I/O 事件类型
enum class IoEvent : std::uint8_t {
    read = 1,   ///< 可读事件
    write = 2,  ///< 可写事件
};

/// 网络运行时
/// 初始化和清理网络库（Windows 需要）
/// 
/// 使用示例：
/// ```cpp
/// void foo() {
///     NetworkRuntime runtime;  // RAII 初始化
///     // ... 网络操作 ...
/// }  // 自动清理
/// ```
class NetworkRuntime {
public:
    NetworkRuntime();
    ~NetworkRuntime();

    NetworkRuntime(const NetworkRuntime&) = delete;
    NetworkRuntime& operator=(const NetworkRuntime&) = delete;

private:
#ifdef _WIN32
    WSADATA data_{};
#endif
};

/// 关闭 socket
void close_socket(socket_handle socket) noexcept;

/// 设置 socket 为非阻塞模式
void set_non_blocking(socket_handle socket);

/// 检查是否为 "would block" 错误
/// @return true 如果操作会阻塞（非阻塞模式下的正常情况）
bool would_block() noexcept;

/// 获取最后的 socket 错误消息
std::string last_socket_error();

/// Socket RAII 包装器
/// 自动管理 socket 生命周期
/// 
/// 使用示例：
/// ```cpp
/// Socket sock(socket(AF_INET, SOCK_STREAM, 0));
/// if (sock.valid()) {
///     // 使用 socket
/// }  // 自动关闭
/// ```
class Socket {
public:
    Socket() noexcept = default;
    explicit Socket(socket_handle handle) noexcept : handle_(handle) {}
    ~Socket() { reset(); }

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;

    Socket(Socket&& other) noexcept : handle_(other.release()) {}

    Socket& operator=(Socket&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    /// 获取 socket 句柄
    socket_handle get() const noexcept { return handle_; }
    
    /// 检查 socket 是否有效
    bool valid() const noexcept { return handle_ != invalid_socket_handle; }

    /// 释放 socket 句柄所有权
    /// @return socket 句柄，调用者负责关闭
    socket_handle release() noexcept {
        auto handle = handle_;
        handle_ = invalid_socket_handle;
        return handle;
    }

    /// 重置 socket
    /// @param handle 新的 socket 句柄（默认为无效句柄）
    void reset(socket_handle handle = invalid_socket_handle) noexcept {
        if (valid()) {
            close_socket(handle_);
        }
        handle_ = handle;
    }

private:
    socket_handle handle_ = invalid_socket_handle;
};

/// 非阻塞 TCP 连接
/// @param host 主机名或 IP 地址
/// @param port 端口号或服务名
/// @return 连接成功的 socket
/// @throws std::runtime_error 连接失败
Socket connect_tcp_non_blocking(std::string_view host, std::string_view port);

/// 发送 TCP 消息（同步，简单接口）
/// @param host 主机名或 IP 地址
/// @param port 端口号或服务名
/// @param message 要发送的消息
/// @return true 发送成功
bool send_tcp_message(std::string_view host, std::string_view port, std::string_view message) noexcept;

/// 创建 TCP 监听 socket（bind + listen）
/// @param host 监听地址（如 "127.0.0.1" 或 "0.0.0.0"）
/// @param port 监听端口
/// @param backlog listen backlog（默认 16）
/// @return 监听 socket
/// @throws std::runtime_error 失败
Socket tcp_listen(std::string_view host, int port, int backlog = 16);

/// 接受 TCP 连接（阻塞）
/// @param listen_fd 监听 socket
/// @return 客户端 socket（invalid if listen_fd closed）
Socket tcp_accept(socket_handle listen_fd);

/// 创建 UDP socket 并绑定（bind）
/// @param host 监听地址
/// @param port 监听端口
/// @return 绑定的 UDP socket
/// @throws std::runtime_error 失败
Socket udp_bind(std::string_view host, int port);

/// UDP 发送数据
/// @param fd UDP socket 句柄
/// @param host 目标地址
/// @param port 目标端口
/// @param data 要发送的数据
/// @return true 发送成功
bool udp_send_to(socket_handle fd, std::string_view host, int port, std::string_view data) noexcept;

}  // namespace ben_gear::net
