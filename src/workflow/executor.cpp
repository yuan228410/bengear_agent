#include "ben_gear/workflow/executor.hpp"
#include "ben_gear/base/log/logger.hpp"
#include <thread>
#include <chrono>
#include <algorithm>

namespace ben_gear {
namespace workflow {

TaskExecutor::TaskExecutor(std::shared_ptr<base::concurrency::ThreadPool> thread_pool)
    : thread_pool_(std::move(thread_pool)) {
    
    if (!thread_pool_) {
        throw std::invalid_argument("Thread pool cannot be null");
    }
}

TaskResult TaskExecutor::execute_task(TaskPtr task, const TaskContext& ctx) {
    if (!task) {
        return TaskResult::error("Task is null");
    }
    
    return task->execute(ctx);
}

std::future<TaskResult> TaskExecutor::execute_task_async(TaskPtr task, const TaskContext& ctx) {
    if (!task) {
        std::promise<TaskResult> promise;
        promise.set_value(TaskResult::error("Task is null"));
        return promise.get_future();
    }
    
    return thread_pool_->submit([task, ctx]() {
        return task->execute(ctx);
    });
}

std::vector<TaskResult> TaskExecutor::execute_batch(
    const std::vector<TaskPtr>& tasks, 
    const TaskContext& ctx_template) {
    
    if (tasks.empty()) {
        return {};
    }
    
    std::vector<std::future<TaskResult>> futures;
    futures.reserve(tasks.size());
    
    // 提交所有任务
    for (const auto& task : tasks) {
        if (task) {
            futures.push_back(execute_task_async(task, ctx_template));
        }
    }
    
    // 等待所有任务完成
    std::vector<TaskResult> results;
    results.reserve(futures.size());
    
    for (auto& future : futures) {
        results.push_back(future.get());
    }
    
    return results;
}

TaskResult TaskExecutor::execute_task_with_retry(
    TaskPtr task, 
    const TaskContext& ctx,
    const RetryPolicy& policy) {
    
    if (!task) {
        return TaskResult::error("Task is null");
    }
    
    for (size_t attempt = 0; attempt <= policy.max_retries; ++attempt) {
        try {
            auto result = task->execute(ctx);
            if (result.success) {
                return result;
            }
        } catch (const std::exception& e) {
            // 记录错误，继续重试
            log::error_fmt("Task execution failed: task_id={}, attempt={}/{}, error={}", 
                           ctx.task_id, attempt + 1, policy.max_retries + 1, e.what());
        } catch (...) {
            // 记录未知异常
            log::error_fmt("Task execution failed: task_id={}, attempt={}/{}, unknown exception", 
                           ctx.task_id, attempt + 1, policy.max_retries + 1);
        }
        
        // 如果不是最后一次尝试，等待一段时间
        if (attempt < policy.max_retries) {
            uint32_t delay_ms = policy.retry_delay_ms;
            if (policy.exponential_backoff) {
                delay_ms *= (1 << attempt);  // 指数退避
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }
    }
    
    return TaskResult::error("Max retries exceeded");
}

std::shared_ptr<base::concurrency::ThreadPool> TaskExecutor::thread_pool() const {
    return thread_pool_;
}

} // namespace workflow
} // namespace ben_gear
