#pragma once

#include "base/config/settings.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
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

/// 高性能线程池
/// 支持动态调整线程数、队列溢出策略
class ThreadPool {
public:
    /// 构造函数
    explicit ThreadPool(const ThreadPoolConfig& config = {});
    
    /// 析构函数
    ~ThreadPool();
    
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    
    /// 提交任务
    /// @param f 任务函数
    /// @param args 参数
    /// @return future 对象
    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args) 
        -> std::future<std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>> {
        using ReturnType = std::invoke_result_t<std::decay_t<F>, std::decay_t<Args>...>;
        
        // 包装任务
        auto task = std::make_shared<std::packaged_task<ReturnType()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
        auto future = task->get_future();
        
        bool should_execute_directly = false;
        
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            
            if (tasks_.size() >= config_.max_queue_size) {
                switch (config_.overflow_policy) {
                case OverflowPolicy::Abort:
                    throw std::runtime_error("Task queue is full");
                case OverflowPolicy::CallerRuns:
                    should_execute_directly = true;
                    break;
                case OverflowPolicy::DiscardOldest:
                    tasks_.pop();
                    break;
                }
            }

            if (!should_execute_directly) {
                tasks_.emplace([task]() { (*task)(); });
            }
        }
        
        // CallerRuns: 在调用者线程直接执行任务，不经过队列
        if (should_execute_directly) {
            (*task)();
            return future;
        }
        
        // 通知工作线程
        condition_.notify_one();
        
        return future;
    }
    
    /// 批量提交任务
    /// @param begin 开始迭代器
    /// @param end 结束迭代器
    template <typename Iterator>
    void submit_batch(Iterator begin, Iterator end) {
        std::vector<std::function<void()>> overflow_tasks;
        
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);

            for (auto it = begin; it != end; ++it) {
                if (tasks_.size() < config_.max_queue_size) {
                    tasks_.push(*it);
                } else {
                    switch (config_.overflow_policy) {
                    case OverflowPolicy::Abort:
                        throw std::runtime_error("Task queue is full");
                    case OverflowPolicy::CallerRuns:
                        overflow_tasks.push_back(*it);
                        break;
                    case OverflowPolicy::DiscardOldest:
                        tasks_.pop();
                        tasks_.push(*it);
                        break;
                    }
                }
            }
        }

        // CallerRuns: 在调用者线程执行溢出的任务
        for (auto& t : overflow_tasks) {
            t();
        }

        // Notify outside lock to avoid "hurry up and wait"
        condition_.notify_all();
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
    
    /// 尝试从队列获取任务
    bool try_pop_task(std::function<void()>& task);
    
    ThreadPoolConfig config_;
    std::vector<std::thread> threads_;
    std::queue<std::function<void()>> tasks_;
    mutable std::mutex queue_mutex_;
    std::condition_variable condition_;
    std::condition_variable done_condition_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> pause_{false};
    std::atomic<size_t> active_threads_{0};
    std::atomic<size_t> total_tasks_{0};
    std::atomic<size_t> completed_tasks_{0};
};

}  // namespace ben_gear::base::concurrency
