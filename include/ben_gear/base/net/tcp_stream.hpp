#pragma once

#include "ben_gear/base/net/event_loop.hpp"
#include "ben_gear/base/net/socket.hpp"
#include "ben_gear/base/net/task.hpp"

#include <cstddef>
#include <string>
#include <string_view>

namespace ben_gear::net {

/// TCP 流
/// 异步 TCP 连接的封装，提供读写接口
/// 
/// 使用示例：
/// ```cpp
/// EventLoop loop;
/// 
/// // 连接到服务器
/// auto stream = co_await async_connect(loop, "example.com", "80");
/// 
/// // 发送数据
/// co_await stream.write_all("GET / HTTP/1.1\r\n\r\n");
/// 
/// // 接收数据
/// char buffer[4096];
/// auto n = co_await stream.read_some(buffer, sizeof(buffer));
/// ```
class TcpStream {
public:
    TcpStream(EventLoop& loop, Socket socket) noexcept
        : loop_(&loop), socket_(std::move(socket)) {}

    TcpStream(const TcpStream&) = delete;
    TcpStream& operator=(const TcpStream&) = delete;
    TcpStream(TcpStream&&) noexcept = default;
    TcpStream& operator=(TcpStream&&) noexcept = default;

    /// 获取原生 socket 句柄
    socket_handle native_handle() const noexcept { return socket_.get(); }
    
    /// 检查 socket 是否有效
    bool valid() const noexcept { return socket_.valid(); }

    /// 主动关闭 socket，用于丢弃脏连接并唤醒等待中的 I/O。
    void close() noexcept { socket_.reset(); }

    /// 读取数据（部分读取）
    /// @param data 数据缓冲区
    /// @param size 缓冲区大小
    /// @return 实际读取的字节数
    /// @note 可能读取少于 size 字节，需要循环读取直到读取足够数据
    Task<std::size_t> read_some(char* data, std::size_t size);
    
    /// 写入数据（部分写入）
    /// @param data 数据缓冲区
    /// @param size 数据大小
    /// @return 实际写入的字节数
    /// @note 可能写入少于 size 字节，需要循环写入直到写入所有数据
    Task<std::size_t> write_some(const char* data, std::size_t size);
    
    /// 写入所有数据
    /// @param data 要写入的数据
    /// @note 保证写入所有数据，内部会循环调用 write_some
    Task<void> write_all(std::string_view data);

 /// 读取所有数据（循环调用 read_some 直到填满缓冲区）
 Task<void> read_all(char* data, std::size_t size);

 /// 获取关联的 EventLoop
 EventLoop& loop() noexcept { return *loop_; }

private:
    EventLoop* loop_ = nullptr;
    Socket socket_;
};

/// 异步连接到 TCP 服务器
/// @param loop 事件循环
/// @param host 主机名或 IP 地址
/// @param port 端口号或服务名
/// @param timeout 连接超时（默认 10 秒）
/// @return TCP 流
Task<TcpStream> async_connect(EventLoop& loop, std::string host, std::string port,
                              std::chrono::milliseconds timeout = std::chrono::seconds{10});

}  // namespace ben_gear::net
