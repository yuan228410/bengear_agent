#pragma once

#include "ben_gear/base/log/sink.hpp"
#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/format.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <deque>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace ben_gear::log {

// 使用命名空间别名
namespace container = base::container;

/// 异步日志记录器
/// 使用后台线程异步写入日志，避免阻塞主线程
/// 
/// 特性：
/// - 异步写入：日志消息放入队列，后台线程处理
/// - 容量限制：队列满时丢弃旧消息
/// - 多输出：支持同时输出到多个目标（stdout、文件、网络）
/// - 线程安全：所有接口都是线程安全的
/// 
/// 使用示例：
/// ```cpp
/// // 创建日志记录器
/// Logger logger(Level::info, {std::make_shared<StdoutSink>()});
/// 
/// // 记录日志
/// logger.log(Level::info, "Application started");
/// 
/// // 刷新缓冲区
/// logger.flush();
/// ```
class Logger {
public:
    Logger() = default;

    /// 构造日志记录器
    /// @param level 日志级别
    /// @param sinks 输出目标列表
    /// @param capacity 队列容量（默认 8192）
    Logger(Level level, SinkList sinks, std::size_t capacity = 8192)
        : level_(level), sinks_(std::move(sinks)), capacity_(capacity == 0 ? 8192 : capacity), running_(true), worker_([this] { consume(); }) {}

    ~Logger() {
        stop();
    }

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    Logger(Logger&& other) noexcept {
        move_from(std::move(other));
    }

    Logger& operator=(Logger&& other) noexcept {
        if (this != &other) {
            stop();
            move_from(std::move(other));
        }
        return *this;
    }

    /// 检查日志级别是否启用
    /// @param level 要检查的级别
    /// @return true 如果该级别的日志会被记录
    bool enabled(Level level) const noexcept {
        return level_ != Level::off && level >= level_;
    }

    /// 记录日志（string_view 版本，覆盖 const char* / string_view）
    void log(Level level, std::string_view message) {
        if (!enabled(level)) {
            return;
        }
        Record record{level, std::chrono::system_clock::now(), container::String(message.data(), message.size())};
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

    /// 记录日志（std::string 版本，消解 std::string 右值歧义）
    void log(Level level, std::string message) {
        log(level, std::string_view(message));
    }

    /// 记录日志（container::String 版本，零拷贝移动）
    void log(Level level, container::String message) {
        if (!enabled(level)) {
            return;
        }
        Record record{level, std::chrono::system_clock::now(), std::move(message)};
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

    /// 刷新缓冲区
    /// 阻塞直到所有待处理的日志都已写入，或超时
    void flush() {
        std::unique_lock lock(flush_mutex_);
        flush_cv_.wait_for(lock, std::chrono::seconds(5), [&] {
            return pending_.load(std::memory_order_acquire) == 0;
        });
        for (auto& sink : sinks_) {
            sink->flush();
        }
    }

    /// 获取丢弃的日志数量
    std::size_t dropped() const noexcept {
        return dropped_.load(std::memory_order_relaxed);
    }

private:
    void move_from(Logger&& other) noexcept {
        // Ensure the other logger's worker thread is stopped before moving
        other.stop();

        level_ = other.level_;
        sinks_ = std::move(other.sinks_);
        capacity_ = other.capacity_;
        running_ = false;
        pending_.store(0, std::memory_order_relaxed);
    }

    void stop() {
        const bool was_running = running_.exchange(false);
        if (was_running) {
            cv_.notify_all();
            if (worker_.joinable()) {
                worker_.join();
            }
            for (auto& sink : sinks_) {
                sink->flush();
            }
        }
    }

    /// 时间戳缓存（避免频繁格式化）
    struct TimestampCache {
        std::time_t second = 0;
        std::string value;
    };

    /// 后台线程：消费日志队列
    void consume() {
        TimestampCache timestamp_cache;
        for (;;) {
            std::deque<Record> batch;
            {
                std::unique_lock lock(mutex_);
                cv_.wait(lock, [&] { return !running_ || !queue_.empty(); });
                if (!running_ && queue_.empty()) {
                    break;
                }
                batch.swap(queue_);
            }
            for (const auto& record : batch) {
                const auto formatted = format(record, timestamp_cache);
                for (auto& sink : sinks_) {
                    sink->write(record, formatted);
                }
            }
            const auto consumed = batch.size();
            if (consumed > 0 && pending_.fetch_sub(consumed, std::memory_order_acq_rel) == consumed) {
                flush_cv_.notify_all();
            }
        }
    }

    /// 格式化时间戳（带缓存）
    static std::string timestamp(std::chrono::system_clock::time_point time_point, TimestampCache& cache) {
        const auto second = std::chrono::system_clock::to_time_t(time_point);
        if (second == cache.second && !cache.value.empty()) {
            return cache.value;
        }
        std::tm tm{};
        {
            static std::mutex time_mutex;
            std::lock_guard lock(time_mutex);
            if (const auto* local = std::localtime(&second)) {
                tm = *local;
            }
        }
        char buffer[32];
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm);
        cache.second = second;
        cache.value = buffer;
        return cache.value;
    }

    /// 格式化日志记录
    static std::string format(const Record& record, TimestampCache& timestamp_cache) {
        std::string formatted;
        const auto timestamp_value = timestamp(record.timestamp, timestamp_cache);
        formatted.reserve(timestamp_value.size() + record.message.size() + 8);
        formatted.append(timestamp_value);
        formatted.append(" [");
        formatted.append(level_name(record.level));
        formatted.append("] ");
        formatted.append(std::string(record.message.c_str(), record.message.size()));
        return formatted;
    }

    Level level_ = Level::info;              ///< 日志级别
    SinkList sinks_;                         ///< 输出目标列表
    std::size_t capacity_ = 8192;            ///< 队列容量
    std::atomic<bool> running_{false};       ///< 是否运行中
    std::atomic<std::size_t> dropped_{0};    ///< 丢弃的日志数量
    std::atomic<std::size_t> pending_{0};    ///< 待处理的日志数量
    std::deque<Record> queue_;               ///< 日志队列
    std::mutex mutex_;                       ///< 队列互斥锁
    std::condition_variable cv_;             ///< 队列条件变量
    std::mutex flush_mutex_;                 ///< 刷新互斥锁
    std::condition_variable flush_cv_;       ///< 刷新条件变量
    std::thread worker_;                     ///< 后台工作线程
};

/// 日志管理器
/// 全局单例，管理默认日志记录器
/// 
/// 使用示例：
/// ```cpp
/// // 设置日志记录器
/// LogManager::set_logger(std::make_shared<Logger>(Level::info, sinks));
/// 
/// // 记录日志
/// LogManager::log(Level::info, "Message");
/// 
/// // 刷新
/// LogManager::flush();
/// ```
class LogManager {
public:
    /// 设置日志记录器
    static void set_logger(std::shared_ptr<Logger> logger) {
        std::lock_guard lock(mutex());
        instance() = std::move(logger);
    }

    /// 获取日志记录器
    static std::shared_ptr<Logger> get_logger() {
        std::lock_guard lock(mutex());
        return instance();
    }

    static void log(Level level, std::string_view message) {
        auto logger = get_logger();
        if (logger) {
            logger->log(level, message);
        }
    }

    static void log(Level level, std::string message) {
        log(level, std::string_view(message));
    }

    static void log(Level level, container::String message) {
        auto logger = get_logger();
        if (logger) {
            logger->log(level, std::move(message));
        }
    }

    /// 刷新缓冲区
    static void flush() {
        auto logger = get_logger();
        if (logger) {
            logger->flush();
        }
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

/// 便捷日志函数
/// 使用全局日志管理器
/// 
/// 使用示例：
/// ```cpp
/// log::info("Application started");
/// log::debug("Processing item: " + item_name);
/// log::error("Failed to connect: " + error_msg);
/// ```
/// 便捷日志函数（string_view 版本，覆盖 const char* / std::string / string_view）
inline void trace(std::string_view message) { LogManager::log(Level::trace, message); }
inline void debug(std::string_view message) { LogManager::log(Level::debug, message); }
inline void info(std::string_view message) { LogManager::log(Level::info, message); }
inline void warn(std::string_view message) { LogManager::log(Level::warn, message); }
inline void error(std::string_view message) { LogManager::log(Level::error, message); }

/// 便捷日志函数（std::string 版本，消解右值歧义）
inline void trace(std::string message) { LogManager::log(Level::trace, std::move(message)); }
inline void debug(std::string message) { LogManager::log(Level::debug, std::move(message)); }
inline void info(std::string message) { LogManager::log(Level::info, std::move(message)); }
inline void warn(std::string message) { LogManager::log(Level::warn, std::move(message)); }
inline void error(std::string message) { LogManager::log(Level::error, std::move(message)); }

/// 便捷日志函数（container::String 版本，零拷贝移动）
inline void trace(container::String message) { LogManager::log(Level::trace, std::move(message)); }
inline void debug(container::String message) { LogManager::log(Level::debug, std::move(message)); }
inline void info(container::String message) { LogManager::log(Level::info, std::move(message)); }
inline void warn(container::String message) { LogManager::log(Level::warn, std::move(message)); }
inline void error(container::String message) { LogManager::log(Level::error, std::move(message)); }

/// 格式化日志函数（C++20 std::format 风格）
/// 使用 {} 占位符，高性能零拷贝
/// 
/// 使用示例：
/// ```cpp
/// log::info_fmt("User {} logged in from {}", username, ip_address);
/// log::error_fmt("Request failed: status={}, attempt={}", status, attempt);
/// ```
template<typename... Args>
inline void trace_fmt(std::string_view fmt, Args&&... args) {
    LogManager::log(Level::trace, container::format(fmt, std::forward<Args>(args)...));
}

template<typename... Args>
inline void debug_fmt(std::string_view fmt, Args&&... args) {
    LogManager::log(Level::debug, container::format(fmt, std::forward<Args>(args)...));
}

template<typename... Args>
inline void info_fmt(std::string_view fmt, Args&&... args) {
    LogManager::log(Level::info, container::format(fmt, std::forward<Args>(args)...));
}

template<typename... Args>
inline void warn_fmt(std::string_view fmt, Args&&... args) {
    LogManager::log(Level::warn, container::format(fmt, std::forward<Args>(args)...));
}

template<typename... Args>
inline void error_fmt(std::string_view fmt, Args&&... args) {
    LogManager::log(Level::error, container::format(fmt, std::forward<Args>(args)...));
}

/// 流式日志函数
/// 
/// 使用示例：
/// ```cpp
/// log::info_stream() << "User " << username << " logged in";
/// log::error_stream() << "Error: " << error_code << " - " << message;
/// ```
inline container::FormatStream trace_stream() { return container::format_stream(); }
inline container::FormatStream debug_stream() { return container::format_stream(); }
inline container::FormatStream info_stream() { return container::format_stream(); }
inline container::FormatStream warn_stream() { return container::format_stream(); }
inline container::FormatStream error_stream() { return container::format_stream(); }

}  // namespace ben_gear::log
