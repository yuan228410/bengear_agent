#pragma once

#include "base/net/cancel.hpp"
#include "base/net/socket.hpp"
#include "base/net/task.hpp"
#include "base/net/wakeup_fd.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <coroutine>
#include <cstddef>
#include <cstdint>
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

    // 用于 IOCP 异步传输（Windows 优先，其他平台回退到就绪模式）
    // 当 transfer_buf != nullptr 时，EventLoop 使用 IOCP 完成模式
    char* transfer_buf = nullptr;
#ifdef _WIN32
    DWORD transfer_len = 0;
    DWORD transfer_result = 0;  // 实际传输的字节数
#else
    uint32_t transfer_len = 0;
    uint32_t transfer_result = 0;
#endif
    int error_code = 0;         // 0=成功，非0=WSAGetLastError / errno

    bool cancelled = false;

#ifdef _WIN32
    OVERLAPPED overlapped{};  // IOCP 异步 I/O；通过 io_pending map 按 OVERLAPPED* 查找 IoOpPtr，无需 CONTAINING_RECORD
#endif
};

/// 定时器操作结构
struct TimerOperation {
    std::chrono::steady_clock::time_point deadline;
    std::coroutine_handle<> continuation;
};

// 对象池 deleter（完整类型，operator() 定义在 event_loop.cpp）
struct IoOpDeleter {
    void* pool_ctx = nullptr;
    void operator()(IoOperation* op);
};
struct TimerOpDeleter {
    void* pool_ctx = nullptr;
    void operator()(TimerOperation* op);
};
using IoOpPtr = std::unique_ptr<IoOperation, IoOpDeleter>;
using TimerOpPtr = std::unique_ptr<TimerOperation, TimerOpDeleter>;

/// 入站操作（MPSC 队列节点）
struct InboundOp {
    enum class Tag { io, timer, task } tag;
    IoOpPtr io;
    TimerOpPtr timer;
    std::function<void()> task_func;
    InboundOp* next = nullptr;
};

// 对象池获取函数（free function，避免在 awaiter 定义时 EventLoop 尚未完整）
IoOpPtr acquire_io(EventLoop& loop);
TimerOpPtr acquire_timer(EventLoop& loop);

/// I/O 等待器（就绪模式，用于 wait_read/wait_write）
class IoAwaiter {
public:
    IoAwaiter(EventLoop& loop, socket_handle socket, IoEvent event)
        : loop_(loop) {
        operation_ = acquire_io(loop_);
        op_raw_ = operation_.get();
        operation_->socket = socket;
        operation_->event = event;
    }
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> handle);
    void await_resume() const {
        if (op_raw_->cancelled)
            throw ResponseTimeoutError("I/O operation cancelled: fd closed by response timeout");
    }
private:
    EventLoop& loop_;
    IoOpPtr operation_;
    IoOperation* op_raw_;
};

/// 异步读等待器（完成模式，用于 read_some）
class ReadAwaiter {
public:
    ReadAwaiter(EventLoop& loop, socket_handle fd, char* buf, size_t size)
        : loop_(loop) {
        operation_ = acquire_io(loop_);
        op_raw_ = operation_.get();
        operation_->socket = fd;
        operation_->event = IoEvent::read;
        operation_->transfer_buf = buf;
        operation_->transfer_len = static_cast<decltype(IoOperation::transfer_len)>(size);
    }
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> handle);
    size_t await_resume() const {
        if (op_raw_->cancelled)
            throw ResponseTimeoutError("read_some cancelled");
        if (op_raw_->error_code)
            throw std::system_error(op_raw_->error_code, std::system_category(),
                                    "read_some failed");
        return op_raw_->transfer_result;
    }
private:
    EventLoop& loop_;
    IoOpPtr operation_;
    IoOperation* op_raw_;
};

/// 异步写等待器（完成模式，用于 write_some）
class WriteAwaiter {
public:
    WriteAwaiter(EventLoop& loop, socket_handle fd, const char* buf, size_t size)
        : loop_(loop) {
        operation_ = acquire_io(loop_);
        op_raw_ = operation_.get();
        operation_->socket = fd;
        operation_->event = IoEvent::write;
        operation_->transfer_buf = const_cast<char*>(buf);
        operation_->transfer_len = static_cast<decltype(IoOperation::transfer_len)>(size);
    }
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> handle);
    size_t await_resume() const {
        if (op_raw_->cancelled)
            throw ResponseTimeoutError("write_some cancelled");
        if (op_raw_->error_code)
            throw std::system_error(op_raw_->error_code, std::system_category(),
                                    "write_some failed");
        return op_raw_->transfer_result;
    }
private:
    EventLoop& loop_;
    IoOpPtr operation_;
    IoOperation* op_raw_;
};

/// 定时器等待器
class TimerAwaiter {
public:
    TimerAwaiter(EventLoop& loop, std::chrono::steady_clock::time_point deadline)
        : loop_(loop) {
        operation_ = acquire_timer(loop_);
        op_raw_ = operation_.get();
        operation_->deadline = deadline;
    }
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> handle);
    void await_resume() const {}
    TimerOperation& operation() { return *op_raw_; }
private:
    EventLoop& loop_;
    TimerOpPtr operation_;
    TimerOperation* op_raw_;
};

/// 事件循环
///
/// 跨平台：
/// - Linux: epoll
/// - macOS: kqueue
/// - Windows: IOCP（优先） + select（回退）
class EventLoop {
public:
    EventLoop();
    ~EventLoop();

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    // --- 就绪模式（兼容） ---
    IoAwaiter wait_read(socket_handle socket) {
        return {*this, socket, IoEvent::read};
    }
    IoAwaiter wait_write(socket_handle socket) {
        return {*this, socket, IoEvent::write};
    }

    // --- 完成模式（IOCP 原生） ---
    ReadAwaiter read_some(socket_handle fd, char* buf, size_t size) {
        return {*this, fd, buf, size};
    }
    WriteAwaiter write_some(socket_handle fd, const char* buf, size_t size) {
        return {*this, fd, buf, size};
    }

    TimerAwaiter sleep_for(std::chrono::milliseconds delay) {
        return {*this, std::chrono::steady_clock::now() + delay};
    }

    void close_after(socket_handle fd, std::chrono::milliseconds delay);
    void cancel_close(socket_handle fd);
    void set_cancel_socket(socket_handle fd);
    socket_handle get_cancel_socket() const;
    void submit(IoOpPtr operation);
    void submit(TimerOpPtr operation);
    void submit_task(std::function<void()> func);

    /// 将入站操作节点归还到对象池（替代 delete）
    void recycle_inbound(InboundOp* op);

    // 对象池获取函数声明为友元（定义见 event_loop.cpp），可访问私有 Impl
    friend IoOpPtr acquire_io(EventLoop& loop);
    friend TimerOpPtr acquire_timer(EventLoop& loop);
    friend struct IoOpDeleter;
    friend struct TimerOpDeleter;
    void run_once(std::chrono::milliseconds timeout = std::chrono::milliseconds{100});
    void run();
    void wakeup();
    void stop();
    /// drain 或 stop 后重置 stopped_ 标志，允许再次调用 run()
    void reset_stop();
    void drain(std::chrono::milliseconds timeout = std::chrono::seconds{30});
    bool is_loop_thread() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    // 从对象池获取入站节点（替代 new InboundOp）
    static InboundOp* acquire_inbound(Impl& impl);
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

inline void fire_and_forget(EventLoop& loop, Task<void> task) {
    auto shared_task = std::make_shared<Task<void>>(std::move(task));
    shared_task->on_complete([shared_task]() {
        (void)shared_task;
    });
    loop.submit_task([shared_task]() {
        shared_task->resume();
    });
}

} // namespace ben_gear::net
