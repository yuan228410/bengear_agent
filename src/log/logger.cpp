#include "log/logger.hpp"
#include "platform/platform.hpp"
#include "platform/os.hpp"

#include <vector>

namespace ben_gear::log {

namespace container = base::container;

// ==================== 追踪上下文 ====================

std::string& current_trace_id() {
    thread_local std::string trace_id;
    return trace_id;
}

void set_trace_id(std::string id) { current_trace_id() = std::move(id); }
const std::string& get_trace_id() { return current_trace_id(); }
void clear_trace_id() { current_trace_id().clear(); }

// ==================== Logger ====================

Logger::Logger(Level level, SinkList sinks, std::size_t capacity)
    : level_(level), sinks_(std::move(sinks)), capacity_(capacity == 0 ? 8192 : capacity) {
    init_ring();
    running_ = true;
    worker_ = std::thread([this] { consume(); });
}

Logger::~Logger() { stop(); }

Logger::Logger(Logger&& other) noexcept { move_from(std::move(other)); }

Logger& Logger::operator=(Logger&& other) noexcept {
    if (this != &other) { stop(); move_from(std::move(other)); }
    return *this;
}

bool Logger::enabled(Level level) const noexcept {
    return level_ != Level::off && level >= level_;
}

Level Logger::level() const noexcept { return level_; }

void Logger::log(Level level, std::string_view message) {
    if (!enabled(level)) return;
    push(Record{level, std::chrono::system_clock::now(),
                 std::string(message.data(), message.size()),
                 base::platform::process::current_pid(),
                 base::concurrency::current_thread_id(),
                 current_trace_id()});
}

void Logger::log(Level level, std::string message) {
    if (!enabled(level)) return;
    push(Record{level, std::chrono::system_clock::now(),
                 std::move(message),
                 base::platform::process::current_pid(),
                 base::concurrency::current_thread_id(),
                 current_trace_id()});
}

void Logger::flush() {
    // 快速路径：队列已空，直接刷 sink
    if (pending_.load(std::memory_order_acquire) == 0) {
        for (auto& sink : sinks_) sink->flush();
        return;
    }
    std::unique_lock lock(flush_mutex_);
    flush_cv_.wait_for(lock, std::chrono::milliseconds(100), [&] {
        return pending_.load(std::memory_order_acquire) == 0;
    });
    for (auto& sink : sinks_) sink->flush();
}
std::size_t Logger::dropped() const noexcept {
    return dropped_.load(std::memory_order_relaxed);
}

void Logger::push(Record record) {
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

void Logger::move_from(Logger&& other) noexcept {
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

void Logger::stop() {
    const bool was_running = running_.exchange(false);
    if (was_running) {
        cv_.notify_all();
        if (worker_.joinable()) worker_.join();
        for (auto& sink : sinks_) sink->flush();
    }
}

void Logger::consume() {
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


// 日志格式：MM-DD HH:MM:SS [level] [pid:tid] [trace_id] message
// 示例：06-07 09:42:10 [info] [5432:12345] [default-default-abc1..] session created
std::string Logger::format(const Record& record, TimestampCache& cache) {
    std::string out;
    auto ts = timestamp(record.timestamp, cache);
    auto pid_str = std::to_string(record.process_id);
    auto tid_str = std::to_string(record.thread_id);
    auto trace = std::string_view(record.trace_id.data(), record.trace_id.size());
    out.reserve(ts.size() + pid_str.size() + tid_str.size() + trace.size() + record.message.size() + 24);
    out.append(ts);                       // 06-07 09:42:10
    out.append(" [");
    out.append(level_name(record.level)); // info
    out.append("] [");
    out.append(pid_str);                  // 5432
    out.append(":");
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

std::string Logger::timestamp(std::chrono::system_clock::time_point tp, TimestampCache& cache) {
    const auto sec = std::chrono::system_clock::to_time_t(tp);
    if (sec == cache.second && !cache.value.empty()) return cache.value;
    auto tm = base::platform::compat::safe_localtime(sec);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%m-%d %H:%M:%S", &tm);
    cache.second = sec;
    cache.value = buf;
    return cache.value;
}

std::size_t Logger::next_pow2(std::size_t v) noexcept {
    if (v == 0) return 1;
    --v;
    v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16;
    if constexpr (sizeof(std::size_t) > 4) v |= v >> 32;
    return v + 1;
}

void Logger::init_ring() {
    const std::size_t cap = next_pow2(capacity_);
    slots_ = std::make_unique<Slot[]>(cap);
    mask_ = cap - 1;
    head_.store(0, std::memory_order_relaxed);
    tail_.store(0, std::memory_order_relaxed);
    for (std::size_t i = 0; i < cap; ++i) slots_[i].ready.store(false, std::memory_order_relaxed);
}

std::vector<Record> Logger::drain_ring() {
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

// ==================== LogManager ====================

/// 无锁发布：logger 指针用 mutex 保护（初始化冷路径），级别用独立原子发布。
/// 热路径：get_logger() 使用 thread_local 缓存 + epoch 快速路径，
/// 仅在 set_logger() 增加 epoch 时才获取锁刷新缓存（无锁读取）。
void LogManager::set_logger(std::shared_ptr<Logger> logger) {
    const Level lvl = logger ? logger->level() : Level::off;
    level_slot().store(lvl, std::memory_order_relaxed);
    {
        std::lock_guard lock(logger_mutex());
        logger_slot() = std::move(logger);
    }
    epoch().fetch_add(1, std::memory_order_release);  // 唤醒所有线程刷新缓存
}

std::shared_ptr<Logger> LogManager::get_logger() {
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
bool LogManager::enabled(Level level) {
    const Level cur = level_slot().load(std::memory_order_relaxed);
    return level != Level::off && level >= cur;
}

void LogManager::log(Level level, std::string_view message) {
    auto logger = get_logger();
    if (logger) logger->log(level, message);
}

void LogManager::log(Level level, std::string message) {
    auto logger = get_logger();
    if (logger) logger->log(level, std::move(message));
}

void LogManager::flush() {
    auto logger = get_logger();
    if (logger) logger->flush();
}

std::atomic<Level>& LogManager::level_slot() {
    static std::atomic<Level> slot{Level::info};
    return slot;
}

std::shared_ptr<Logger>& LogManager::logger_slot() {
    static std::shared_ptr<Logger> slot;
    return slot;
}

std::mutex& LogManager::logger_mutex() {
    static std::mutex m;
    return m;
}

std::atomic<uint64_t>& LogManager::epoch() {
    static std::atomic<uint64_t> e{0};
    return e;
}

// ==================== 便捷日志函数 ====================

void trace(std::string_view message) { LogManager::log(Level::trace, message); }
void debug(std::string_view message) { LogManager::log(Level::debug, message); }
void info(std::string_view message) { LogManager::log(Level::info, message); }
void warn(std::string_view message) { LogManager::log(Level::warn, message); }
void error(std::string_view message) { LogManager::log(Level::error, message); }

void trace(std::string message) { LogManager::log(Level::trace, std::move(message)); }
void debug(std::string message) { LogManager::log(Level::debug, std::move(message)); }
void info(std::string message) { LogManager::log(Level::info, std::move(message)); }
void warn(std::string message) { LogManager::log(Level::warn, std::move(message)); }
void error(std::string message) { LogManager::log(Level::error, std::move(message)); }
void critical(std::string_view message) { LogManager::log(Level::critical, message); }
void critical(std::string message) { LogManager::log(Level::critical, std::move(message)); }


// ==================== LogStream ====================

void LogStream::flush() {
    if (!flushed_ && !disabled_) {
        flushed_ = true;
        LogManager::log(level_, stream_.str());
    }
}

// ==================== 流式日志 ====================

LogStream trace_stream() {
    if (!LogManager::enabled(Level::trace)) return LogStream{};
    return LogStream(Level::trace);
}
LogStream debug_stream() {
    if (!LogManager::enabled(Level::debug)) return LogStream{};
    return LogStream(Level::debug);
}
LogStream info_stream() {
    if (!LogManager::enabled(Level::info)) return LogStream{};
    return LogStream(Level::info);
}
LogStream warn_stream() {
    if (!LogManager::enabled(Level::warn)) return LogStream{};
    return LogStream(Level::warn);
}
LogStream error_stream() {
    if (!LogManager::enabled(Level::error)) return LogStream{};
    return LogStream(Level::error);
}
LogStream critical_stream() {
    if (!LogManager::enabled(Level::critical)) return LogStream{};
    return LogStream(Level::critical);
}

/// 前端级别判断（用于条件格式化场景）
bool is_enabled(Level level) { return LogManager::enabled(level); }

}  // namespace ben_gear::log
