#include "ben_gear/workflow/executor.hpp"
#include "ben_gear/base/log/logger.hpp"
#include <thread>
#include <chrono>
#include <algorithm>

namespace ben_gear {
namespace workflow {

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

    // I/O 密集型用 std::async，用完线程即销毁，不占线程池
    return std::async(std::launch::async, [task, ctx]() {
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

    for (const auto& task : tasks) {
        if (task) {
            futures.push_back(execute_task_async(task, ctx_template));
        }
    }

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
            log::error_fmt("Task execution failed: task_id={}, attempt={}/{}, error={}",
                           ctx.task_id, attempt + 1, policy.max_retries + 1, e.what());
        } catch (...) {
            log::error_fmt("Task execution failed: task_id={}, attempt={}/{}, unknown exception",
                           ctx.task_id, attempt + 1, policy.max_retries + 1);
        }

        if (attempt < policy.max_retries) {
            uint32_t delay_ms = policy.retry_delay_ms;
            if (policy.exponential_backoff) {
                delay_ms *= (1 << attempt);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }
    }

    return TaskResult::error("Max retries exceeded");
}

} // namespace workflow
} // namespace ben_gear
