#pragma once

#include "ben_gear/base/net/event_loop.hpp"
#include "ben_gear/base/log/logger.hpp"

#include <atomic>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace ben_gear::net {

/// I/O 上下文 — EventLoop + 专属线程的封装
///
/// 设计原则：
/// - 每个 IoContext 拥有一个长驻 EventLoop 和一个专属线程
/// - EventLoop 只做事件分发（I/O 就绪、定时器、协程恢复）
/// - 耗时操作通过 submit_task() 提交到 EventLoop 线程执行
/// - 多个 IoContext 可以分工（io / workflow），高内聚低耦合
///
/// 生命周期：
/// - 构造时只初始化资源，不启动线程
/// - 调用 start() 后启动 EventLoop 线程
/// - 析构时 stop() + join()
///
/// 使用示例：
/// ```cpp
/// IoContext io_ctx("io");
/// auto result = sync_wait(io_ctx.loop(), some_async_task());
/// // 析构时自动停止
/// ```
class IoContext {
public:
    /// 构造 IoContext（不启动线程，避免构造期 this 逃逸）
    /// @param name 线程名称（用于调试和日志）
    explicit IoContext(std::string_view name = "io")
        : loop_(std::make_unique<EventLoop>())
        , name_(name.data(), name.size()) {}

    /// 启动 EventLoop 线程。必须在 IoContext 完整构造后调用。
    ///
    /// 兼容旧调用模式：既可以由拥有者在初始化完成后显式调用，
    /// 也可以由 loop()/submit_task() 在首次使用时惰性启动。
    void start() {
        std::lock_guard<std::mutex> lock(start_mutex_);
        if (thread_.joinable() || stopping_) return;
        loop_->reset_stop();
        thread_ = std::thread([this] {
            log::info_fmt("IoContext [{}] thread started", name_);
            loop_->run();  // 长驻模式，直到 stop()
            log::info_fmt("IoContext [{}] thread stopped", name_);
        });
    }

    /// 析构：优雅停止 EventLoop（等待已提交任务完成）并等待线程结束
    ~IoContext() {
        stop_and_join();
    }

    IoContext(const IoContext&) = delete;
    IoContext& operator=(const IoContext&) = delete;

    /// 获取 EventLoop 引用
    EventLoop& loop() {
        start();
        return *loop_;
    }
    const EventLoop& loop() const {
        const_cast<IoContext*>(this)->start();
        return *loop_;
    }

    /// 从任意线程提交任务到 EventLoop 线程执行（线程安全）
    void submit_task(std::function<void()> func) {
        start();
        loop_->submit_task(std::move(func));
    }

    /// 优雅停止：等待所有已提交任务完成后再停止，并等待线程结束。
    void drain(std::chrono::milliseconds timeout = std::chrono::seconds{30}) {
        stop_and_join(timeout);
    }

    /// 获取上下文名称
    const std::string& name() const { return name_; }

private:
    void stop_and_join(std::chrono::milliseconds timeout = std::chrono::seconds{30}) {
        std::thread local_thread;
        {
            std::lock_guard<std::mutex> lock(start_mutex_);
            if (!thread_.joinable()) return;
            stopping_ = true;
            local_thread = std::move(thread_);
        }
        loop_->drain(timeout);
        local_thread.join();
        {
            std::lock_guard<std::mutex> lock(start_mutex_);
            stopping_ = false;
        }
    }

    std::unique_ptr<EventLoop> loop_;
    std::string name_;
    std::thread thread_;
    mutable std::mutex start_mutex_;
    bool stopping_ = false;
};

}  // namespace ben_gear::net
