#include "ben_gear/base/concurrency/thread_pool.hpp"
#include "ben_gear/base/log/logger.hpp"

namespace ben_gear::base::concurrency {

// ==================== ThreadPool ====================

ThreadPool::ThreadPool(const ThreadPoolConfig& config)
    : config_(config) {
    // 创建工作线程
    const size_t thread_count = config.min_threads;
    threads_.reserve(thread_count);
    
    for (size_t i = 0; i < thread_count; ++i) {
        threads_.emplace_back(&ThreadPool::worker_thread, this);
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
}

void ThreadPool::wait() {
    std::unique_lock<std::mutex> lock(queue_mutex_);
    done_condition_.wait(lock, [this] {
        return tasks_.empty() && active_threads_.load() == 0;
    });
}

ThreadPoolStats ThreadPool::stats() const {
    ThreadPoolStats stats;
    stats.total_tasks = total_tasks_.load(std::memory_order_relaxed);
    stats.completed_tasks = completed_tasks_.load(std::memory_order_relaxed);
    
    // 准确统计：active_threads 是正在执行任务的线程数
    stats.active_threads = active_threads_.load(std::memory_order_relaxed);
    
    std::lock_guard<std::mutex> lock(queue_mutex_);
    stats.queued_tasks = tasks_.size();
    
    // idle_threads = 总线程数 - 活跃线程数（包含等待中的线程）
    stats.idle_threads = (threads_.size() > stats.active_threads) 
        ? (threads_.size() - stats.active_threads) 
        : 0;

    return stats;
}

void ThreadPool::pause() {
    pause_.store(true);
}

void ThreadPool::resume() {
    pause_.store(false);
    condition_.notify_all();
}

void ThreadPool::shutdown() {
    stop_.store(true);
    condition_.notify_all();
    
    for (auto& thread : threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    threads_.clear();
}

void ThreadPool::worker_thread() {
    while (true) {
        std::function<void()> task;
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            
            // 等待任务或停止信号
            condition_.wait(lock, [this] {
                return stop_.load() || (!pause_.load() && !tasks_.empty());
            });
            
            // 检查是否停止
            if (stop_.load() && tasks_.empty()) {
                return;
            }
            
            // 获取任务
            if (!tasks_.empty()) {
                task = std::move(tasks_.front());
                tasks_.pop();
            }
        }
        
        // 执行任务
        if (task) {
            active_threads_.fetch_add(1);
            total_tasks_.fetch_add(1);
            
            try {
                task();
            } catch (const std::exception& e) {
                // 记录异常信息
                ::ben_gear::log::error_fmt("ThreadPool task failed: {}", e.what());
            } catch (...) {
                // 记录未知异常
                ::ben_gear::log::error_fmt("ThreadPool task failed: unknown exception");
            }
            
            active_threads_.fetch_sub(1);
            completed_tasks_.fetch_add(1);
            
            // 通知等待的线程
            done_condition_.notify_all();
        }
    }
}

bool ThreadPool::try_pop_task(std::function<void()>& task) {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    
    if (tasks_.empty()) {
        return false;
    }
    
    task = std::move(tasks_.front());
    tasks_.pop();
    return true;
}

}  // namespace ben_gear::base::concurrency
