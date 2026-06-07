#pragma once

#include "ben_gear/base/net/cancel.hpp"
#include "ben_gear/base/net/socket.hpp"
#include "ben_gear/base/net/task.hpp"
#include "ben_gear/base/net/wakeup_fd.hpp"

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
#include "ben_gear/base/log/logger.hpp"
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

/// I/O 等待器
class IoAwaiter {
public:
    IoAwaiter(EventLoop& loop, socket_handle socket, IoEvent event) noexcept
        : loop_(loop), operation_(std::make_shared<IoOperation>(IoOperation{socket, event, {}})) {}

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
        : loop_(loop), operation_(std::make_shared<TimerOperation>(TimerOperation{std::chrono::steady_clock::now() + delay, {}})) {}

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
    /// 与 stop() 的区别：stop() 立即设置停止标志，drain() 等待入站队列和挂起任务处理完
    /// @param timeout 最大等待时间（默认 30 秒），超时后强制停止并打印警告
    void drain(std::chrono::milliseconds timeout = std::chrono::seconds{30});

    /// 当前线程是否为 EventLoop 线程
    /// 用于 sync_wait 死锁检测
    bool is_loop_thread() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};


// ---------------------------------------------------------------------------
// sync_wait — 在指定 EventLoop 上运行协程并阻塞等待结果
//
// 事件驱动，零轮询：
// 1. 将协程包装为 shared_ptr，设置 on_complete 回调
// 2. submit_task 提交到 EventLoop 线程启动
// 3. 协程完成时 FinalAwaiter 触发 on_complete → 设置 promise → future.get() 返回
//
// ⚠️ 约束：只能在非 EventLoop 线程调用！
//    在 EventLoop 线程调用会导致死锁（future.get 阻塞 EventLoop，协程永远无法完成）
// ---------------------------------------------------------------------------

namespace detail {

/// 设置完成回调并提交到 EventLoop
/// Task 被 shared_ptr 持有，on_complete 回调中也持有，保证生命周期
template <typename T>
void submit_with_completion(EventLoop& loop,
                            std::shared_ptr<Task<T>> task,
                            std::shared_ptr<std::promise<T>> promise) {
    // 事件驱动：协程完成时 FinalAwaiter 调用此回调，直接设置 promise
    // 异常安全：task->result() 异常 → set_exception
    //           promise 已被设置（如同步完成时）→ 忽略 future_error
    task->on_complete([task, promise]() {
        try {
            if constexpr (std::is_void_v<T>) {
                task->result();
                promise->set_value();
            } else {
                promise->set_value(task->result());
            }
        } catch (const std::future_error&) {
            // promise 已被设置（如协程同步完成时 submit_task 回调先设了值），忽略
        } catch (...) {
            try {
                promise->set_exception(std::current_exception());
            } catch (const std::future_error&) {
                // 同上，promise 已被设置，忽略
            }
        }
    });

    // 提交到 EventLoop 线程：resume 启动协程
    // 协程挂起后由 EventLoop I/O 事件驱动，完成后 FinalAwaiter 触发 on_complete
    loop.submit_task([task]() {
        task->resume();
    });
}

} // namespace detail

template <typename T>
T sync_wait(EventLoop& loop, Task<T> task) {
    // 死锁检测：禁止在 EventLoop 线程内调用 sync_wait
    // 因为 future.get() 会阻塞当前线程，如果在 EventLoop 线程内调用，
    // EventLoop 无法继续驱动协程，导致永久死锁
    if (loop.is_loop_thread()) {
        throw std::logic_error("sync_wait: cannot be called from EventLoop thread (would deadlock)");
    }

    auto shared_task = std::make_shared<Task<T>>(std::move(task));
    auto promise = std::make_shared<std::promise<T>>();
    auto future = promise->get_future();

    detail::submit_with_completion(loop, shared_task, promise);

    return future.get();
}

}  // namespace ben_gear::net
