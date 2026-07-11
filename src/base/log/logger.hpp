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
        : level_(level), sinks_(std::move(sinks)), capacity_(capacity == 0 ? 8192 : capacity) {
        init_ring();
        running_ = true;
        worker_ = std::thread([this] { consume(); });
    }

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

    Level level() const noexcept { return level_; }

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
        const std::size_t cap = mask_ + 1;
        for (;;) {
            std::size_t t = tail_.load(std::memory_order_acquire);
            const std::size_t h = head_.load(std::memory_order_acquire);
            if (t - h >= cap) {  // 队列满：丢弃最新记录（无锁近似）
                dropped_.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            if (tail_.compare_exchange_weak(t, t + 1, std::memory_order_acquire)) {
                const std::size_t idx = t & mask_;
                // 等待上一轮同槽位被消费（极罕见）
                while (slots_[idx].ready.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }
                slots_[idx].rec = std::move(record);
                slots_[idx].ready.store(true, std::memory_order_release);
                pending_.fetch_add(1, std::memory_order_relaxed);
                cv_.notify_one();
                return;
            }
        }
    }

    void move_from(Logger&& other) noexcept {
        other.stop();
        level_ = other.level_;
        sinks_ = std::move(other.sinks_);
        capacity_ = other.capacity_;
        init_ring();
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
            std::vector<Record> batch = drain_ring();
            if (!batch.empty()) {
                for (const auto& record : batch) {
                    auto formatted = format(record, ts_cache);
                    for (auto& sink : sinks_) sink->write(record, formatted);
                }
                const auto n = batch.size();
                if (pending_.fetch_sub(n, std::memory_order_acq_rel) == n) {
                    flush_cv_.notify_all();
                }
                continue;
            }
            if (!running_) {
                // 停止后再次排空，确保不丢尾部日志
                batch = drain_ring();
                if (batch.empty()) break;
                for (const auto& record : batch) {
                    auto formatted = format(record, ts_cache);
                    for (auto& sink : sinks_) sink->write(record, formatted);
                }
                const auto n = batch.size();
                if (pending_.fetch_sub(n, std::memory_order_acq_rel) == n) {
                    flush_cv_.notify_all();
                }
                continue;
            }
            std::unique_lock lock(cv_mutex_);
            cv_.wait(lock, [&] { return !running_ || head_.load() != tail_.load(); });
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

    static std::size_t next_pow2(std::size_t v) noexcept {
        if (v == 0) return 1;
        --v;
        v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16;
        if (sizeof(std::size_t) > 4) v |= v >> 32;
        return v + 1;
    }
    void init_ring() {
        const std::size_t cap = next_pow2(capacity_);
        slots_ = std::make_unique<Slot[]>(cap);
        mask_ = cap - 1;
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
        for (std::size_t i = 0; i < cap; ++i) slots_[i].ready.store(false, std::memory_order_relaxed);
    }
    std::vector<Record> drain_ring() {
        std::vector<Record> batch;
        for (;;) {
            const std::size_t h = head_.load(std::memory_order_acquire);
            const std::size_t t = tail_.load(std::memory_order_acquire);
            if (h == t) break;
            const std::size_t idx = h & mask_;
            if (!slots_[idx].ready.load(std::memory_order_acquire)) break;
            batch.push_back(std::move(slots_[idx].rec));
            slots_[idx].ready.store(false, std::memory_order_release);
            head_.store(h + 1, std::memory_order_release);
        }
        return batch;
    }

};

// ==================== 日志管理器 ====================

class LogManager {
public:
    /// 无锁发布：logger 指针用 mutex 保护（初始化冷路径），级别用独立原子发布。
    /// 热路径：get_logger() 使用 thread_local 缓存 + epoch 快速路径，
    /// 仅在 set_logger() 增加 epoch 时才获取锁刷新缓存（无锁读取）。
    static void set_logger(std::shared_ptr<Logger> logger) {
        const Level lvl = logger ? logger->level() : Level::off;
        level_slot().store(lvl, std::memory_order_relaxed);
        {
            std::lock_guard lock(logger_mutex());
            logger_slot() = std::move(logger);
        }
        epoch().fetch_add(1, std::memory_order_release);  // 唤醒所有线程刷新缓存
    }

    static std::shared_ptr<Logger> get_logger() {
        thread_local std::shared_ptr<Logger> cached;
        thread_local uint64_t cached_epoch = 0;
        uint64_t current = epoch().load(std::memory_order_acquire);
        if (cached_epoch == current && cached) {
            return cached;  // 快速路径：无锁，无原子操作
        }
        // 慢速路径：获取锁并刷新缓存
        std::lock_guard lock(logger_mutex());
        current = epoch().load(std::memory_order_acquire);
        if (cached_epoch != current) {
            cached = logger_slot();
            cached_epoch = current;
        }
        return cached;
    }

    /// 前端级别判断，避免无谓格式化开销（无锁）
    static bool enabled(Level level) {
        const Level cur = level_slot().load(std::memory_order_relaxed);
        return level != Level::off && level >= cur;
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
    static std::atomic<Level>& level_slot() {
        static std::atomic<Level> slot{Level::info};
        return slot;
    }

    static std::shared_ptr<Logger>& logger_slot() {
        static std::shared_ptr<Logger> slot;
        return slot;
    }

    static std::mutex& logger_mutex() {
        static std::mutex m;
        return m;
    }

    static std::atomic<uint64_t>& epoch() {
        static std::atomic<uint64_t> e{0};
        return e;
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
