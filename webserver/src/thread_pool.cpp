#include "webserver/thread_pool.hpp"
#include "webserver/logging.hpp"

#include <system_error>

namespace ws {

// ============ ThreadPool ============

ThreadPool::ThreadPool(size_t num_threads)
    : stop_(false)
{
    if (num_threads == 0) {
        num_threads = std::thread::hardware_concurrency();
        if (num_threads == 0) num_threads = 4;
    }

    log::info_fmt("ThreadPool: creating {} threads", num_threads);

    for (size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back([this, i] {
            log::debug_fmt("ThreadPool: worker {} started", i);
            while (true) {
                Task task;
                {
                    std::unique_lock<std::mutex> lock(queue_mutex_);
                    condition_.wait(lock, [this] {
                        return stop_ || !tasks_.empty();
                    });

                    if (stop_ && tasks_.empty()) {
                        log::debug_fmt("ThreadPool: worker {} exiting", i);
                        return;
                    }

                    task = std::move(tasks_.front());
                    tasks_.pop();
                }

                if (task) {
                    try {
                        task();
                    } catch (const std::exception& e) {
                        log::error_fmt("ThreadPool: task exception: {}", e.what());
                    }
                }
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        stop_ = true;
    }
    condition_.notify_all();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    log::info_fmt("ThreadPool: destroyed");
}

void ThreadPool::enqueue(Task task) {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        if (stop_) {
            throw std::runtime_error("ThreadPool: enqueue on stopped pool");
        }
        tasks_.emplace(std::move(task));
    }
    condition_.notify_one();
}

size_t ThreadPool::size() const noexcept {
    return workers_.size();
}

}  // namespace ws
