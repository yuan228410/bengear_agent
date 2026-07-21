#include "concurrency/thread_pool.hpp"
#include <mutex>
#include <chrono>
#include "log/logger.hpp"

#include <thread>

namespace ben_gear::base::concurrency {

// ==================== helpers ====================

size_t ThreadPool::next_power_of_2(size_t n) {
    if (n == 0) return 1;
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n |= n >> 32;
    return n + 1;
}

void ThreadPool::cpu_pause() {
#if defined(_M_X86) || defined(__x86_64__)
    __builtin_ia32_pause();
#elif defined(_M_ARM) || defined(__aarch64__)
    __asm__ __volatile__("yield" ::: "memory");
#endif
}

// ==================== ThreadPool ====================

ThreadPool::ThreadPool(const ThreadPoolConfig& config)
    : config_(config)
    , ring_(next_power_of_2(config.max_queue_size)) {
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
    std::unique_lock lock(wait_mutex_);
    wait_cv_.wait(lock, [this] {
        return pending_.load(std::memory_order_acquire) == 0 &&
               active_threads_.load(std::memory_order_acquire) == 0;
    });
}

ThreadPoolStats ThreadPool::stats() const {
    ThreadPoolStats s;
    s.total_tasks = total_tasks_.load(std::memory_order_relaxed);
    s.completed_tasks = completed_tasks_.load(std::memory_order_relaxed);
    s.active_threads = active_threads_.load(std::memory_order_relaxed);

    // ring_.size() 使用原子读，快照值即可
    s.queued_tasks = ring_.size();

    s.idle_threads = (threads_.size() > s.active_threads)
        ? (threads_.size() - s.active_threads)
        : 0;

    return s;
}

void ThreadPool::pause() {
    pause_.store(true, std::memory_order_release);
}

void ThreadPool::resume() {
    pause_.store(false, std::memory_order_release);
}

void ThreadPool::shutdown() {
    stop_.store(true, std::memory_order_release);
    cv_.notify_all();

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
        bool got_task = false;

        {
            std::unique_lock lock(cv_mutex_);
            // 等待：有新任务、暂停或停止信号
            cv_.wait(lock, [this] {
                return stop_.load(std::memory_order_acquire) ||
                       pause_.load(std::memory_order_acquire) ||
                       !ring_.empty();
            });
        }

        // 检查停止
        if (stop_.load(std::memory_order_acquire) && ring_.empty()) {
            return;
        }

        // 检查暂停
        if (pause_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        {
            SpinlockGuard lock(pop_lock_);
            got_task = ring_.pop(task);
        }

        if (!got_task) {
            continue;
        }

        // 执行任务
        active_threads_.fetch_add(1, std::memory_order_release);
        total_tasks_.fetch_add(1, std::memory_order_relaxed);

        try {
            task();
        } catch (const std::exception& e) {
            ::ben_gear::log::error_fmt("ThreadPool task failed: {}", e.what());
        } catch (...) {
            ::ben_gear::log::error_fmt("ThreadPool task failed: unknown exception");
        }

        active_threads_.fetch_sub(1, std::memory_order_release);
        completed_tasks_.fetch_add(1, std::memory_order_relaxed);
        pending_.fetch_sub(1, std::memory_order_release);

        // 唤醒 wait() 检查完成状态
        wait_cv_.notify_all();
    }
}

}  // namespace ben_gear::base::concurrency