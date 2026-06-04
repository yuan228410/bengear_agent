#pragma once

#include "ben_gear/base/net/socket.hpp"
#include "ben_gear/base/net/task.hpp"

#include <chrono>
#include <coroutine>
#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

namespace ben_gear::net {

/// 前向声明
class EventLoop;

/// I/O 操作结构
/// 用于跟踪挂起的 I/O 操作
struct IoOperation {
    socket_handle socket = invalid_socket_handle;  ///< socket 句柄
    IoEvent event = IoEvent::read;                  ///< 事件类型（读/写）
    std::coroutine_handle<> continuation;           ///< 协程句柄，用于恢复执行
};

/// 定时器操作结构
/// 用于跟踪挂起的定时器操作
struct TimerOperation {
    std::chrono::steady_clock::time_point deadline;  ///< 截止时间
    std::coroutine_handle<> continuation;            ///< 协程句柄
};

/// I/O 等待器
/// 协程 awaiter，用于等待 socket 可读/可写
class IoAwaiter {
public:
    IoAwaiter(EventLoop& loop, socket_handle socket, IoEvent event) noexcept
        : loop_(loop), operation_(std::make_shared<IoOperation>(IoOperation{socket, event, {}})) {}

    /// 是否立即完成（总是返回 false，需要挂起）
    bool await_ready() const noexcept { return false; }
    
    /// 挂起协程，注册 I/O 事件
    void await_suspend(std::coroutine_handle<> handle);
    
    /// 恢复时调用（无返回值）
    void await_resume() const noexcept {}

private:
    EventLoop& loop_;
    std::shared_ptr<IoOperation> operation_;
};

/// 定时器等待器
/// 协程 awaiter，用于延时
class TimerAwaiter {
public:
    TimerAwaiter(EventLoop& loop, std::chrono::milliseconds delay) noexcept
        : loop_(loop), operation_(std::make_shared<TimerOperation>(TimerOperation{std::chrono::steady_clock::now() + delay, {}})) {}

    /// 是否立即完成（检查是否已超时）
    bool await_ready() const noexcept;
    
    /// 挂起协程，注册定时器
    void await_suspend(std::coroutine_handle<> handle);
    
    /// 恢复时调用（无返回值）
    void await_resume() const noexcept {}

private:
    EventLoop& loop_;
    std::shared_ptr<TimerOperation> operation_;
};

/// 事件循环
/// 基于 I/O 多路复用的异步事件循环
/// 
/// 使用示例：
/// ```cpp
/// EventLoop loop;
/// 
/// // 运行异步任务
/// auto result = loop.run(async_task());
/// 
/// // 等待 socket 可读
/// co_await loop.wait_read(socket);
/// 
/// // 延时 100ms
/// co_await loop.sleep_for(std::chrono::milliseconds(100));
/// ```
class EventLoop {
public:
    EventLoop();
    ~EventLoop();

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    /// 等待 socket 可读
    /// @param socket socket 句柄
    /// @return I/O 等待器
    IoAwaiter wait_read(socket_handle socket) noexcept { 
        return {*this, socket, IoEvent::read}; 
    }
    
    /// 等待 socket 可写
    /// @param socket socket 句柄
    /// @return I/O 等待器
    IoAwaiter wait_write(socket_handle socket) noexcept { 
        return {*this, socket, IoEvent::write}; 
    }
    
    /// 延时
    /// @param delay 延时时长
    /// @return 定时器等待器
    TimerAwaiter sleep_for(std::chrono::milliseconds delay) noexcept { 
        return {*this, delay}; 
    }

    /// 提交 I/O 操作
    void submit(std::shared_ptr<IoOperation> operation);
    
    /// 提交定时器操作
    void submit(std::shared_ptr<TimerOperation> operation);
    
    /// 运行一次事件循环
    /// @param timeout 超时时间（默认 100ms）
    void run_once(std::chrono::milliseconds timeout = std::chrono::milliseconds{100});
    
    /// 运行事件循环（阻塞）
    void run();

    /// 运行异步任务并等待完成
    /// @param task 异步任务
    /// @return 任务结果
    template <typename T>
    T run(Task<T> task) {
        task.resume();
        while (!task.done()) {
            run_once();
        }
        return task.result();
    }

    /// 运行无返回值的异步任务
    void run(Task<void> task) {
        task.resume();
        while (!task.done()) {
            run_once();
        }
        task.result();
    }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ben_gear::net
