#pragma once
#include <chrono>

#include "net/event_loop.hpp"
#include "log/logger.hpp"

#include <atomic>
#include <stdexcept>
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
/// - 构造时启动线程
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
    /// 构造 IoContext 并启动 EventLoop 线程
    /// @param name 线程名称（用于调试和日志）
    explicit IoContext(const std::string& name = "io");

    ~IoContext();

    IoContext(const IoContext&) = delete;
    IoContext& operator=(const IoContext&) = delete;

    /// 获取 EventLoop 引用
    EventLoop& loop();
    const EventLoop& loop() const;

    /// 从任意线程提交任务到 EventLoop 线程执行（线程安全）
    void submit_task(std::function<void()> func) {
        loop_->submit_task(std::move(func));
    }

    /// 优雅停止：等待所有已提交任务完成后再停止
    /// 可从任意线程调用，调用后不再接受新任务
    /// 与析构的区别：drain() 只停止 EventLoop，不 join 线程
    void drain(std::chrono::milliseconds timeout = std::chrono::seconds{30}) {
        loop_->drain(timeout);
    }

    /// 获取上下文名称
    const std::string& name() const { return name_; }

private:
    std::unique_ptr<EventLoop> loop_;
    std::string name_;
    std::thread thread_;
};

}  // namespace ben_gear::net
