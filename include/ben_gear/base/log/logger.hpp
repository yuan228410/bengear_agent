#pragma once

#include "ben_gear/base/log/sink.hpp"
#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/format.hpp"
#include "ben_gear/base/concurrency/tid.hpp"

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

namespace ben_gear::log {

namespace container = base::container;

// ==================== 追踪上下文 ====================
// 线程本地追踪标签，格式：
//   主会话：user-workspace-session_id
//   工作流任务：user-workspace-session_id:wf:task_id
//   子Agent：user-workspace-session_id:agent:name
//   全局：空（日志中不显示 trace 段）

inline std::string& current_trace_id() {
    thread_local std::string trace_id;
    return trace_id;
}

inline void set_trace_id(std::string id) { current_trace_id() = std::move(id); }
inline const std::string& get_trace_id() { return current_trace_id(); }
inline void clear_trace_id() { current_trace_id().clear(); }

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

    Logger(Level level, SinkList sinks, std::size_t capacity = 8192)
        : level_(level), sinks_(std::move(sinks)), capacity_(capacity == 0 ? 8192 : capacity),
          running_(true), worker_([this] { consume(); }) {}

    ~Logger() { stop(); }

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    Logger(Logger&& other) noexcept { move_from(std::move(other)); }

    Logger& operator=(Logger&& other) noexcept {
        if (this != &other) { stop(); move_from(std::move(other)); }
        return *this;
    }

    bool enabled(Level level) const noexcept {
        return level_ != Level::off && level >= level_;
    }

    void log(Level level, std::string_view message) {
        if (!enabled(level)) return;
        push(Record{level, std::chrono::system_clock::now(),
                     container::String(message.data(), message.size()),
                     base::concurrency::current_thread_id(),
                     container::String(current_trace_id().c_str())});
    }

    void log(Level level, std::string message) {
        log(level, std::string_view(message));
    }

    void log(Level level, container::String message) {
        if (!enabled(level)) return;
        push(Record{level, std::chrono::system_clock::now(),
                     std::move(message),
                     base::concurrency::current_thread_id(),
                     container::String(current_trace_id().c_str())});
    }

    void flush() {
        std::unique_lock lock(flush_mutex_);
        flush_cv_.wait_for(lock, std::chrono::seconds(5), [&] {
            return pending_.load(std::memory_order_acquire) == 0;
        });
        for (auto& sink : sinks_) sink->flush();
    }

    std::size_t dropped() const noexcept {
        return dropped_.load(std::memory_order_relaxed);
    }

private:
    void push(Record record) {
        {
            std::lock_guard lock(mutex_);
            if (queue_.size() >= capacity_) {
                queue_.pop_front();
                dropped_.fetch_add(1, std::memory_order_relaxed);
            } else {
                pending_.fetch_add(1, std::memory_order_relaxed);
            }
            queue_.push_back(std::move(record));
        }
        cv_.notify_one();
    }

    void move_from(Logger&& other) noexcept {
        other.stop();
        level_ = other.level_;
sinks_ = std::move(other.sinks_);
capacity_ = other.capacity_;
pending_.store(0, std::memory_order_relaxed);
 // 重启 worker 线程，确保移动后的 Logger 可用
 running_ = true;
 worker_ = std::thread([this] { consume(); });
}

    void stop() {
        const bool was_running = running_.exchange(false);
        if (was_running) {
            cv_.notify_all();
            if (worker_.joinable()) worker_.join();
            for (auto& sink : sinks_) sink->flush();
        }
    }

    struct TimestampCache {
        std::time_t second = 0;
        std::string value;
    };

    void consume() {
        TimestampCache ts_cache;
        for (;;) {
            std::deque<Record> batch;
            {
                std::unique_lock lock(mutex_);
                cv_.wait(lock, [&] { return !running_ || !queue_.empty(); });
                if (!running_ && queue_.empty()) break;
                batch.swap(queue_);
            }
            for (const auto& record : batch) {
                auto formatted = format(record, ts_cache);
                for (auto& sink : sinks_) sink->write(record, formatted);
            }
            const auto n = batch.size();
            if (n > 0 && pending_.fetch_sub(n, std::memory_order_acq_rel) == n) {
                flush_cv_.notify_all();
            }
        }
    }

    // 日志格式：MM-DD HH:MM:SS [level] [tid] [trace_id] message
    // 示例：06-07 09:42:10 [info] [12345] [default-default-abc1..] session created
    //       06-07 09:42:10 [error] [12346] TLS handshake failed

    static std::string format(const Record& record, TimestampCache& cache) {
        std::string out;
        auto ts = timestamp(record.timestamp, cache);
        auto tid_str = std::to_string(record.thread_id);
        auto trace = std::string_view(record.trace_id.data(), record.trace_id.size());
        out.reserve(ts.size() + tid_str.size() + trace.size() + record.message.size() + 16);
        out.append(ts);                       // 06-07 09:42:10
        out.append(" [");
        out.append(level_name(record.level)); // info
        out.append("] [");
        out.append(tid_str);                  // 12345
        out.append("]");
        out.append(" [");
        if (!trace.empty()) {
            out.append(trace.data(), trace.size());
        } else {
            out.append("global");
        }
        out.append("]");
        out.append(" ");
        out.append(record.message.c_str(), record.message.size());
        return out;
    }

    static std::string timestamp(std::chrono::system_clock::time_point tp, TimestampCache& cache) {
        const auto sec = std::chrono::system_clock::to_time_t(tp);
        if (sec == cache.second && !cache.value.empty()) return cache.value;
        std::tm tm{};
        {
            static std::mutex m;
            std::lock_guard lock(m);
            if (const auto* local = std::localtime(&sec)) tm = *local;
        }
        char buf[32];
        std::strftime(buf, sizeof(buf), "%m-%d %H:%M:%S", &tm);
        cache.second = sec;
        cache.value = buf;
        return cache.value;
    }

    Level level_ = Level::info;
    SinkList sinks_;
    std::size_t capacity_ = 8192;
    std::atomic<bool> running_{false};
    std::atomic<std::size_t> dropped_{0};
    std::atomic<std::size_t> pending_{0};
    std::deque<Record> queue_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::mutex flush_mutex_;
    std::condition_variable flush_cv_;
    std::thread worker_;
};

// ==================== 日志管理器 ====================

class LogManager {
public:
    static void set_logger(std::shared_ptr<Logger> logger) {
        std::lock_guard lock(mutex());
        instance() = std::move(logger);
    }

    static std::shared_ptr<Logger> get_logger() {
        std::lock_guard lock(mutex());
        return instance();
    }

    /// 前端级别判断，避免无谓格式化开销
    static bool enabled(Level level) {
        auto logger = get_logger();
        return logger && logger->enabled(level);
    }

    static void log(Level level, std::string_view message) {
        auto logger = get_logger();
        if (logger) logger->log(level, message);
    }

    static void log(Level level, std::string message) {
        log(level, std::string_view(message));
    }

    static void log(Level level, container::String message) {
        auto logger = get_logger();
        if (logger) logger->log(level, std::move(message));
    }

    static void flush() {
        auto logger = get_logger();
        if (logger) logger->flush();
    }

private:
    static std::shared_ptr<Logger>& instance() {
        static std::shared_ptr<Logger> logger;
        return logger;
    }
    static std::mutex& mutex() {
        static std::mutex value;
        return value;
    }
};

// ==================== 便捷日志函数 ====================

inline void trace(std::string_view message) { LogManager::log(Level::trace, message); }
inline void debug(std::string_view message) { LogManager::log(Level::debug, message); }
inline void info(std::string_view message) { LogManager::log(Level::info, message); }
inline void warn(std::string_view message) { LogManager::log(Level::warn, message); }
inline void error(std::string_view message) { LogManager::log(Level::error, message); }

inline void trace(std::string message) { LogManager::log(Level::trace, std::move(message)); }
inline void debug(std::string message) { LogManager::log(Level::debug, std::move(message)); }
inline void info(std::string message) { LogManager::log(Level::info, std::move(message)); }
inline void warn(std::string message) { LogManager::log(Level::warn, std::move(message)); }
inline void error(std::string message) { LogManager::log(Level::error, std::move(message)); }

inline void trace(container::String message) { LogManager::log(Level::trace, std::move(message)); }
inline void debug(container::String message) { LogManager::log(Level::debug, std::move(message)); }
inline void info(container::String message) { LogManager::log(Level::info, std::move(message)); }
inline void warn(container::String message) { LogManager::log(Level::warn, std::move(message)); }
inline void error(container::String message) { LogManager::log(Level::error, std::move(message)); }

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

inline container::FormatStream trace_stream() { return container::format_stream(); }
inline container::FormatStream debug_stream() { return container::format_stream(); }
inline container::FormatStream info_stream() { return container::format_stream(); }
inline container::FormatStream warn_stream() { return container::format_stream(); }
inline container::FormatStream error_stream() { return container::format_stream(); }

/// 前端级别判断（用于条件格式化场景）
inline bool is_enabled(Level level) { return LogManager::enabled(level); }

}  // namespace ben_gear::log
