#pragma once

#include "task.hpp"
#include "types.hpp"
#include "ben_gear/base/concurrency/thread_pool.hpp"
#include <future>
#include <vector>
#include <memory>

namespace ben_gear {
namespace workflow {

/// 任务执行器（支持共享线程池，避免 std::async 线程爆炸）
class TaskExecutor {
public:
    /// 构造函数（可选传入共享线程池）
    explicit TaskExecutor(std::shared_ptr<base::concurrency::ThreadPool> pool = nullptr)
        : pool_(std::move(pool)) {}

    TaskExecutor(const TaskExecutor&) = delete;
    TaskExecutor& operator=(const TaskExecutor&) = delete;

    /// 同步执行单个任务
    TaskResult execute_task(TaskPtr task, const TaskContext& ctx);

    /// 异步执行任务（优先使用线程池，无线程池时降级为 std::async）
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

private:
    std::shared_ptr<base::concurrency::ThreadPool> pool_;
};

} // namespace workflow
} // namespace ben_gear
