#pragma once

#include "base/log/sink.hpp"
#include "base/container/string.hpp"
#include "base/container/format.hpp"
#include "base/concurrency/tid.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace ben_gear::log {

namespace container = base::container;

// ==================== 追踪上下文 ====================
// 线程本地追踪标签，格式：
//   主会话：user-workspace-session_id
//   工作流任务：user-workspace-session_id:wf:task_id
//   子Agent：user-workspace-session_id:agent:name
//   全局：空（日志中不显示 trace 段）

std::string& current_trace_id();

void set_trace_id(std::string id);
const std::string& get_trace_id();
void clear_trace_id();

/// RAII 追踪标签守卫，析构时自动恢复旧标签
class TraceGuard {
public:
    explicit TraceGuard(std::string new_id)
        : saved_(std::move(current_trace_id())) {
        current_trace_id() = std::move(new_id);
    }
    ~TraceGuard() { current_trace_id() = std::move(saved_); }
    TraceGuard(const TraceGuard&) = delete;
    TraceGuard& operator=(const TraceGuard&) = delete;
private:
    std::string saved_;
};

// ==================== 异步日志记录器 ====================

class Logger {
public:
    Logger() = default;

    Logger(Level level, SinkList sinks, std::size_t capacity = 8192);

    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    Logger(Logger&& other) noexcept;

    Logger& operator=(Logger&& other) noexcept;

    bool enabled(Level level) const noexcept;

    Level level() const noexcept;

    void log(Level level, std::string_view message);

    void log(Level level, std::string message);

    void log(Level level, container::String message);

    void flush();

    std::size_t dropped() const noexcept;

private:
    void push(Record record);

    void move_from(Logger&& other) noexcept;

    void stop();

    struct TimestampCache {
        std::time_t second = 0;
        std::string value;
    };

    void consume();

    // 日志格式：MM-DD HH:MM:SS [level] [tid] [trace_id] message
    // 示例：06-07 09:42:10 [info] [12345] [default-default-abc1..] session created
    //       06-07 09:42:10 [error] [12346] TLS handshake failed

    static std::string format(const Record& record, TimestampCache& cache);

    static std::string timestamp(std::chrono::system_clock::time_point tp, TimestampCache& cache);

    Level level_ = Level::info;
    SinkList sinks_;
    std::size_t capacity_ = 8192;
    std::atomic<bool> running_{false};
    std::atomic<std::size_t> dropped_{0};
    std::atomic<std::size_t> pending_{0};

    // 无锁有界 MPSC 环形缓冲：多生产者（日志线程）并发入队，单消费者（worker）出队
    struct Slot {
        std::atomic<bool> ready{false};
        Record rec;
    };
    std::unique_ptr<Slot[]> slots_;  // 预分配环形槽（避免热路径堆分配；Slot 含 atomic 不可移动）
    std::size_t mask_ = 0;
    std::atomic<std::size_t> head_{0};  // 消费者游标
    std::atomic<std::size_t> tail_{0};  // 生产者游标
    std::mutex cv_mutex_;
    std::condition_variable cv_;
    std::mutex flush_mutex_;
    std::condition_variable flush_cv_;
    std::thread worker_;

    static std::size_t next_pow2(std::size_t v) noexcept;
    void init_ring();
    std::vector<Record> drain_ring();

};

// ==================== 日志管理器 ====================

class LogManager {
public:
    /// 无锁发布：logger 指针用 mutex 保护（初始化冷路径），级别用独立原子发布。
    /// 热路径：get_logger() 使用 thread_local 缓存 + epoch 快速路径，
    /// 仅在 set_logger() 增加 epoch 时才获取锁刷新缓存（无锁读取）。
    static void set_logger(std::shared_ptr<Logger> logger);

    static std::shared_ptr<Logger> get_logger();

    /// 前端级别判断，避免无谓格式化开销（无锁）
    static bool enabled(Level level);

    static void log(Level level, std::string_view message);

    static void log(Level level, std::string message);

    static void log(Level level, container::String message);

    static void flush();

private:
    static std::atomic<Level>& level_slot();
    static std::shared_ptr<Logger>& logger_slot();
    static std::mutex& logger_mutex();
    static std::atomic<uint64_t>& epoch();
};

// ==================== 便捷日志函数 ====================

void trace(std::string_view message);
void debug(std::string_view message);
void info(std::string_view message);
void warn(std::string_view message);
void error(std::string_view message);

void trace(std::string message);
void debug(std::string message);
void info(std::string message);
void warn(std::string message);
void error(std::string message);

void trace(container::String message);
void debug(container::String message);
void info(container::String message);
void warn(container::String message);
void error(container::String message);

// ==================== 格式化日志（前端级别判断，避免无谓格式化） ====================

template<typename... Args>
inline void trace_fmt(std::string_view fmt, Args&&... args) {
    if (!LogManager::enabled(Level::trace)) return;
    LogManager::log(Level::trace, container::format(fmt, std::forward<Args>(args)...));
}

template<typename... Args>
inline void debug_fmt(std::string_view fmt, Args&&... args) {
    if (!LogManager::enabled(Level::debug)) return;
    LogManager::log(Level::debug, container::format(fmt, std::forward<Args>(args)...));
}

template<typename... Args>
inline void info_fmt(std::string_view fmt, Args&&... args) {
    if (!LogManager::enabled(Level::info)) return;
    LogManager::log(Level::info, container::format(fmt, std::forward<Args>(args)...));
}

template<typename... Args>
inline void warn_fmt(std::string_view fmt, Args&&... args) {
    if (!LogManager::enabled(Level::warn)) return;
    LogManager::log(Level::warn, container::format(fmt, std::forward<Args>(args)...));
}

template<typename... Args>
inline void error_fmt(std::string_view fmt, Args&&... args) {
    if (!LogManager::enabled(Level::error)) return;
    LogManager::log(Level::error, container::format(fmt, std::forward<Args>(args)...));
}

// ==================== 流式日志 ====================

container::FormatStream trace_stream();
container::FormatStream debug_stream();
container::FormatStream info_stream();
container::FormatStream warn_stream();
container::FormatStream error_stream();

/// 前端级别判断（用于条件格式化场景）
bool is_enabled(Level level);

}  // namespace ben_gear::log
