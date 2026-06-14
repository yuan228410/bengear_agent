#pragma once

#include "webserver/core.hpp"
#include "webserver/event_loop.hpp"
#include "webserver/thread_pool.hpp"
#include "webserver/router.hpp"
#include "webserver/http_parser.hpp"
#include "webserver/response_builder.hpp"

#include <memory>
#include <string>
#include <atomic>
#include <netinet/in.h>

namespace ws {

/// 核心 HTTP 服务端
/// 整合事件循环、线程池、路由，提供高性能 HTTP 服务
class HttpServer {
public:
    /// @param addr 监听地址（如 "0.0.0.0"）
    /// @param port 监听端口（如 8080）
    /// @param num_threads 工作线程数，0 = 硬件并发数
    HttpServer(std::string_view addr, uint16_t port, size_t num_threads = 0);
    ~HttpServer();

    // 禁止拷贝
    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    /// 获取路由表引用（用于注册路由）
    [[nodiscard]] Router& router() noexcept { return router_; }

    /// 启动服务器（阻塞，直到收到停止信号）
    void start();

    /// 优雅关闭
    void stop() noexcept;

    /// 检查是否正在运行
    [[nodiscard]] bool is_running() const noexcept { return running_.load(); }

private:
    std::string addr_;
    uint16_t port_;
    std::atomic<bool> running_{false};

    int server_fd_ = -1;
    struct sockaddr_in server_addr_;

    EventLoop event_loop_;
    ThreadPool thread_pool_;
    Router router_;
    RequestParser parser_;

    // 客户端连接信息
    struct ClientConnection {
        int fd;
        std::string buffer;
    };

    /// 初始化监听 socket
    void init_socket();

    /// 接受新连接
    void on_accept();

    /// 处理客户端可读事件
    void on_client_read(int client_fd);

    /// 处理客户端可写事件（发送响应）
    void on_client_write(int client_fd, HttpResponse response);

    /// 关闭客户端连接
    void close_client(int client_fd);

    /// 信号处理
    static void handle_signal(int sig);
};

}  // namespace ws
