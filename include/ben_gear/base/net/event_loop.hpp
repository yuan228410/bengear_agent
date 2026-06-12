#pragma once

#include "ben_gear/base/net/cancel.hpp"
#include "ben_gear/base/net/socket.hpp"
#include "ben_gear/base/net/task.hpp"
#include "ben_gear/base/net/wakeup_fd.hpp"
#include "ben_gear/base/memory/pool.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <coroutine>
#include <cstddef>
#include <functional>
#include <stdexcept>
#include <memory>
#include <mutex>
#include <future>
#include <type_traits>
#include <vector>

namespace ben_gear::net {

/// 响应超时异常（由 close_after 触发，不应重试）
class ResponseTimeoutError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class EventLoop;

/// I/O 操作结构
struct IoOperation {
    socket_handle socket = invalid_socket_handle;
    IoEvent event = IoEvent::read;
    std::coroutine_handle<> continuation;
    bool cancelled = false;
};

/// 定时器操作结构
struct TimerOperation {
    std::chrono::steady_clock::time_point deadline;
    std::coroutine_handle<> continuation;
};

/// 入站操作（MPSC 队列节点）
struct InboundOp {
    enum class Tag { io, timer, task } tag;
    std::shared_ptr<IoOperation> io;
    std::shared_ptr<TimerOperation> timer;
    std::function<void()> task_func;
    InboundOp* next = nullptr;
};

/// EventLoop 专用内存池（单例）
/// 为 IoOperation / TimerOperation 提供池化分配
/// 使用 allocate_shared 让控制块+对象一起从池分配
class EventLoopPool {
public:
    static EventLoopPool& instance() {
        static EventLoopPool pool;
        return pool;
    }

    base::memory::MemoryPool& pool() { return pool_; }

private:
    EventLoopPool() = default;
    // IoOperation 约 32 字节，TimerOperation 约 24 字节
    // allocate_shared 一次性分配 sizeof(T) + 控制块（约 48 字节），用 128 字节桶足够
    base::memory::MemoryPool pool_{base::memory::PoolConfig{64, 1024, 65536, 256, true}};
};

/// I/O 等待器
class IoAwaiter {
public:
    IoAwaiter(EventLoop& loop, socket_handle socket, IoEvent event) noexcept
        : loop_(loop) {
        // 池化分配：allocate_shared 从内存池分配控制块+对象
        auto& p = EventLoopPool::instance();
        operation_ = std::allocate_shared<IoOperation>(
            base::memory::PoolAllocator<IoOperation>(p.pool()),
            IoOperation{socket, event, {}});
    }

    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> handle);
    void await_resume() const {
        if (operation_->cancelled) {
            throw ResponseTimeoutError("I/O operation cancelled: fd closed by response timeout");
        }
    }

private:
    EventLoop& loop_;
    std::shared_ptr<IoOperation> operation_;
};

/// 定时器等待器
class TimerAwaiter {
public:
    TimerAwaiter(EventLoop& loop, std::chrono::milliseconds delay) noexcept
        : loop_(loop) {
        auto& p = EventLoopPool::instance();
        operation_ = std::allocate_shared<TimerOperation>(
            base::memory::PoolAllocator<TimerOperation>(p.pool()),
            TimerOperation{std::chrono::steady_clock::now() + delay, {}});
    }

    bool await_ready() const noexcept;
    void await_suspend(std::coroutine_handle<> handle);
    void await_resume() const noexcept {}

private:
    EventLoop& loop_;
    std::shared_ptr<TimerOperation> operation_;
};

/// 事件循环
/// 基于 I/O 多路复用的异步事件循环，配合协程实现事件驱动
///
/// 运行模式：长驻模式 run() 持续运行直到 stop()
/// 跨线程安全：submit_task() / wakeup() / stop() 可从任意线程调用
class EventLoop {
public:
    EventLoop();
    ~EventLoop();

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    IoAwaiter wait_read(socket_handle socket) noexcept {
        return {*this, socket, IoEvent::read};
    }

    IoAwaiter wait_write(socket_handle socket) noexcept {
        return {*this, socket, IoEvent::write};
    }

    TimerAwaiter sleep_for(std::chrono::milliseconds delay) noexcept {
        return {*this, delay};
    }

    void close_after(socket_handle fd, std::chrono::milliseconds delay);
    void cancel_close(socket_handle fd);
    void submit(std::shared_ptr<IoOperation> operation);
    void submit(std::shared_ptr<TimerOperation> operation);
    void submit_task(std::function<void()> func);
    void run_once(std::chrono::milliseconds timeout = std::chrono::milliseconds{100});
    void run();
    void wakeup();
    void stop();

    /// 优雅停止：等待所有已提交任务完成后再停止
    void drain(std::chrono::milliseconds timeout = std::chrono::seconds{30});

    /// 当前线程是否为 EventLoop 线程
    bool is_loop_thread() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ---------------------------------------------------------------------------
// sync_wait — 在指定 EventLoop 上运行协程并阻塞等待结果
// ---------------------------------------------------------------------------

namespace detail {

template <typename T>
void submit_with_completion(EventLoop& loop,
    std::shared_ptr<Task<T>> task,
    std::shared_ptr<std::promise<T>> promise) {
    task->on_complete([task, promise]() {
        try {
            if constexpr (std::is_void_v<T>) {
                task->result();
                promise->set_value();
            } else {
                promise->set_value(task->result());
            }
        } catch (const std::future_error&) {
        } catch (...) {
            try {
                promise->set_exception(std::current_exception());
            } catch (const std::future_error&) {
            }
        }
    });

    loop.submit_task([task]() {
        task->resume();
    });
}

} // namespace detail

template <typename T>
T sync_wait(EventLoop& loop, Task<T> task) {
    if (loop.is_loop_thread()) {
        throw std::logic_error("sync_wait: cannot be called from EventLoop thread (would deadlock)");
    }

    auto shared_task = std::make_shared<Task<T>>(std::move(task));
    auto promise = std::make_shared<std::promise<T>>();
    auto future = promise->get_future();

    detail::submit_with_completion(loop, shared_task, promise);

    return future.get();
}

} // namespace ben_gear::net
