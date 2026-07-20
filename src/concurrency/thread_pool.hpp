#pragma once

#include "config/settings.hpp"
#include "concurrency/lock_free.hpp"
#include "concurrency/spinlock.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>

namespace ben_gear::base::concurrency {

/// 任务队列溢出时的处理策略
enum class OverflowPolicy {
    Abort,          ///< 抛异常（默认，保持向后兼容）
    CallerRuns,     ///< 调用者在当前线程直接执行任务
    DiscardOldest,  ///< 丢弃队列中最旧的任务，插入新任务
};

/// 线程池配置
struct ThreadPoolConfig {
    size_t min_threads = 2;                        ///< 最小线程数
    size_t max_threads = 8;                        ///< 最大线程数
    size_t max_queue_size = 1024;                  ///< 最大任务队列大小
    std::chrono::milliseconds idle_timeout{5000};  ///< 空闲线程超时时间
    OverflowPolicy overflow_policy = OverflowPolicy::Abort;  ///< 队列满时的处理策略
};

/// 从 ThreadPoolSettings 转换为 ThreadPoolConfig
inline ThreadPoolConfig to_thread_pool_config(const config::ThreadPoolSettings& s) {
    ThreadPoolConfig cfg;
    cfg.min_threads = static_cast<size_t>(s.min_threads);
    cfg.max_threads = static_cast<size_t>(s.max_threads);
    cfg.max_queue_size = static_cast<size_t>(s.max_queue_size);
    cfg.idle_timeout = std::chrono::milliseconds(s.idle_timeout_ms);
    cfg.overflow_policy = static_cast<OverflowPolicy>(s.overflow_policy);
    return cfg;
}

/// 线程池统计信息
struct ThreadPoolStats {
    size_t total_tasks = 0;         ///< 总任务数
    size_t completed_tasks = 0;     ///< 已完成任务数
    size_t active_threads = 0;      ///< 活跃线程数
    size_t idle_threads = 0;        ///< 空闲线程数
    size_t queued_tasks = 0;        ///< 队列中任务数
};

/// 高性能线程池（基于无锁环形缓冲区）
/// 使用 SPSC DynamicLockFreeRingBuffer + spinlock 实现 MPSC 安全：
/// - push 侧 spinlock 序列化多个生产者
/// - pop 侧 spinlock 序列化多个消费者
/// - worker 空闲时通过 condition_variable 阻塞等待，避免 busy-spin
class ThreadPool {
public:
    explicit ThreadPool(const ThreadPoolConfig& config = {});
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    /// 提交任务
    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>> {
        using ReturnType = std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>;

        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        auto future = task->get_future();

        bool should_execute_directly = false;

        {
            push_lock_.lock();

            if (ring_.full()) {
                // 分级退避：快速自旋 → yield，给消费者线程调度机会
                // 保持在 ~1-2ms 以内，仅覆盖线程启动延迟，不掩盖持续过载
                for (int spin = 0; spin < 128; ++spin) {
                    push_lock_.unlock();
                    if (spin < 16) {
                        cpu_pause();
                    } else {
                        std::this_thread::yield();
                    }
                    push_lock_.lock();
                    if (!ring_.full()) break;
                }
            }

            // 自旋后仍满？应用溢出策略
            if (ring_.full()) {
                switch (config_.overflow_policy) {
                case OverflowPolicy::Abort:
                    push_lock_.unlock();
                    throw std::runtime_error("Task queue is full");
                case OverflowPolicy::CallerRuns:
                    should_execute_directly = true;
                    break;
                case OverflowPolicy::DiscardOldest: {
                    std::function<void()> discarded;
                    {
                        SpinlockGuard pop_lock(pop_lock_);
                        if (ring_.pop(discarded)) {
                            // 丢弃的任务不会被 worker 执行，需手动递减 pending_
                            pending_.fetch_sub(1, std::memory_order_release);
                        }
                    }
                    break;
                }
                }
            }

            if (!should_execute_directly) {
                ring_.push(std::function<void()>([task]() { (*task)(); }));
                pending_.fetch_add(1, std::memory_order_release);
            }

            push_lock_.unlock();
        }

        // 通知一个 worker 取任务
        if (!should_execute_directly) {
            cv_.notify_one();
        }

        if (should_execute_directly) {
            (*task)();
            return future;
        }

        return future;
    }

    /// 批量提交任务
    template <typename Iterator>
    void submit_batch(Iterator begin, Iterator end) {
        std::vector<std::function<void()>> overflow_tasks;

        {
            SpinlockGuard lock(push_lock_);

            for (auto it = begin; it != end; ++it) {
                if (!ring_.full()) {
                    ring_.push(std::move(*it));
                    pending_.fetch_add(1, std::memory_order_release);
                } else {
                    switch (config_.overflow_policy) {
                    case OverflowPolicy::Abort:
                        throw std::runtime_error("Task queue is full");
                    case OverflowPolicy::CallerRuns:
                        overflow_tasks.push_back(std::move(*it));
                        break;
                    case OverflowPolicy::DiscardOldest: {
                        std::function<void()> discarded;
                        {
                            SpinlockGuard pop_lock(pop_lock_);
                            if (ring_.pop(discarded)) {
                                pending_.fetch_sub(1, std::memory_order_release);
                            }
                        }
                        ring_.push(std::move(*it));
                        pending_.fetch_add(1, std::memory_order_release);
                        break;
                    }
                    }
                }
            }
        }

        for (auto& t : overflow_tasks) {
            t();
        }

        // 通知 worker 取任务
        cv_.notify_all();
    }

    /// 等待所有任务完成
    void wait();

    /// 获取统计信息
    ThreadPoolStats stats() const;

    /// 暂停
    void pause();

    /// 恢复
    void resume();

    /// 关闭
    void shutdown();

private:
    /// 工作线程函数
    void worker_thread();

    /// 向上取整到 2 的幂
    static size_t next_power_of_2(size_t n);

    /// CPU pause hint
    static void cpu_pause();

    ThreadPoolConfig config_;
    std::vector<std::thread> threads_;

    /// 无锁环形缓冲区（SPSC），push/pop 分别由独立 spinlock 保护以实现 MPSC
    DynamicLockFreeRingBuffer<std::function<void()>> ring_;

    /// push 侧 spinlock（多生产者序列化）
    mutable Spinlock push_lock_;

    /// pop 侧 spinlock（多消费者序列化）
    mutable Spinlock pop_lock_;

    std::atomic<bool> stop_{false};
    std::atomic<bool> pause_{false};
    std::atomic<size_t> active_threads_{0};
    std::atomic<size_t> total_tasks_{0};
    std::atomic<size_t> completed_tasks_{0};

    /// 飞行中任务数（已提交但未完成），用于 wait() 跟踪
    std::atomic<size_t> pending_{0};

    /// 阻塞等待条件变量（worker 空闲时 wait，submit 时 notify）
    std::mutex cv_mutex_;
    std::condition_variable cv_;
    /// wait() 完成条件变量
    std::mutex wait_mutex_;
    std::condition_variable wait_cv_;
};

}  // namespace ben_gear::base::concurrency