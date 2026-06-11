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
    log::debug_fmt("executor: executing task, id={}", task->id());
    auto result = task->execute(ctx);
    log::debug_fmt("executor: task done, id={}, success={}", task->id(), result.success);
    return result;
}

std::future<TaskResult> TaskExecutor::execute_task_async(TaskPtr task, const TaskContext& ctx) {
    if (!task) {
        std::promise<TaskResult> promise;
        promise.set_value(TaskResult::error("Task is null"));
        return promise.get_future();
    }

    // 优先使用共享线程池，避免 std::async 线程爆炸
    if (pool_) {
        return pool_->submit([task, ctx]() {
            log::debug_fmt("executor: thread pool task started, id={}", task->id());
            auto result = task->execute(ctx);
            log::debug_fmt("executor: thread pool task done, id={}, success={}", task->id(), result.success);
            return result;
        });
    }

    // 降级：使用 std::async（I/O 密集型场景）
    return std::async(std::launch::async, [task, ctx]() {
        log::debug_fmt("executor: async task started, id={}", task->id());
        auto result = task->execute(ctx);
        log::debug_fmt("executor: async task done, id={}, success={}", task->id(), result.success);
        return result;
    });
}

std::vector<TaskResult> TaskExecutor::execute_batch(
    const std::vector<TaskPtr>& tasks,
    const TaskContext& ctx_template) {

    if (tasks.empty()) {
        return {};
    }

    log::info_fmt("executor: batch executing {} tasks", tasks.size());

    std::vector<std::future<TaskResult>> futures;
    futures.reserve(tasks.size());

    for (const auto& task : tasks) {
        if (task) {
            // 每个任务独立 ctx，设置各自的 task_id，避免共享覆盖
            TaskContext ctx = ctx_template;
            ctx.task_id = task->id();
            futures.push_back(execute_task_async(task, ctx));
        }
    }

    std::vector<TaskResult> results;
    results.reserve(futures.size());
    for (auto& future : futures) {
        results.push_back(future.get());
    }

    log::info_fmt("executor: batch done, {} results", results.size());

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
            log::info_fmt("executor: retrying task, id={}, attempt={}/{}, delay={}ms",
                          ctx.task_id, attempt + 2, policy.max_retries + 1, delay_ms);
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }
    }

    return TaskResult::error("Max retries exceeded");
}

} // namespace workflow
} // namespace ben_gear
