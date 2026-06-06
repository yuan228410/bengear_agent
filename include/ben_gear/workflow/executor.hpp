#pragma once

#include "task.hpp"
#include "types.hpp"
#include <future>
#include <vector>
#include <memory>

namespace ben_gear {
namespace workflow {

/// 任务执行器（I/O 密集型，使用 std::async 而非线程池，用完即销毁）
class TaskExecutor {
public:
    TaskExecutor() = default;

    TaskExecutor(const TaskExecutor&) = delete;
    TaskExecutor& operator=(const TaskExecutor&) = delete;

    /// 同步执行单个任务
    TaskResult execute_task(TaskPtr task, const TaskContext& ctx);

    /// 异步执行任务（std::async，I/O 密集型无需线程池）
    std::future<TaskResult> execute_task_async(TaskPtr task, const TaskContext& ctx);

    /// 批量执行任务（并行）
    std::vector<TaskResult> execute_batch(
        const std::vector<TaskPtr>& tasks,
        const TaskContext& ctx_template);

    /// 带重试的执行
    TaskResult execute_task_with_retry(
        TaskPtr task,
        const TaskContext& ctx,
        const RetryPolicy& policy);
};

} // namespace workflow
} // namespace ben_gear
