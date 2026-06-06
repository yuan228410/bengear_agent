#pragma once

#include "task.hpp"
#include "types.hpp"
#include "ben_gear/base/concurrency/thread_pool.hpp"
#include <future>
#include <vector>
#include <memory>

namespace ben_gear {
namespace workflow {

// 任务执行器
class TaskExecutor {
public:
    // 使用共享的线程池（推荐）
    explicit TaskExecutor(std::shared_ptr<base::concurrency::ThreadPool> thread_pool);
    
    // 禁止拷贝
    TaskExecutor(const TaskExecutor&) = delete;
    TaskExecutor& operator=(const TaskExecutor&) = delete;
    
    // 同步执行单个任务
    TaskResult execute_task(TaskPtr task, const TaskContext& ctx);
    
    // 异步执行任务
    std::future<TaskResult> execute_task_async(TaskPtr task, const TaskContext& ctx);
    
    // 批量执行任务（并行）
    std::vector<TaskResult> execute_batch(
        const std::vector<TaskPtr>& tasks, 
        const TaskContext& ctx_template);
    
    // 带重试的执行
    TaskResult execute_task_with_retry(
        TaskPtr task, 
        const TaskContext& ctx,
        const RetryPolicy& policy);
    
    // 获取线程池
    std::shared_ptr<base::concurrency::ThreadPool> thread_pool() const;
    
private:
    std::shared_ptr<base::concurrency::ThreadPool> thread_pool_;
};

} // namespace workflow
} // namespace ben_gear
