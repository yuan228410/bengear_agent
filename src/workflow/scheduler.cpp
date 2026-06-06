#include "ben_gear/workflow/scheduler.hpp"
#include <iostream>

namespace ben_gear {
namespace workflow {

WorkflowScheduler::WorkflowScheduler(
    DAG dag, 
    std::shared_ptr<TaskExecutor> executor,
    ErrorHandlingStrategy error_strategy)
    : dag_(std::move(dag))
    , executor_(std::move(executor))
    , error_strategy_(error_strategy) {
}

WorkflowResult WorkflowScheduler::run() {
    running_ = true;
    paused_ = false;
    cancelled_ = false;
    
    WorkflowResult result;
    result.status = WorkflowStatus::RUNNING;
    
    // 检查 DAG 是否为空
    if (dag_.empty()) {
        result.success = true;
        result.status = WorkflowStatus::SUCCESS;
        running_ = false;
        return result;
    }
    
    // 检查是否有环
    if (dag_.has_cycle()) {
        result.success = false;
        result.error_message = "DAG contains cycle";
        result.status = WorkflowStatus::FAILED;
        running_ = false;
        return result;
    }
    
    // 执行任务
    while (completed_tasks_.size() < dag_.size()) {
        // 检查是否应该停止
        if (should_stop()) {
            result.status = cancelled_ ? WorkflowStatus::CANCELLED : WorkflowStatus::PAUSED;
            running_ = false;
            return result;
        }
        
        // 获取可执行任务
        auto ready_tasks = dag_.get_ready_tasks(completed_tasks_);
        
        if (ready_tasks.empty()) {
            // 没有可执行任务，但还有未完成的任务 -> 说明有任务失败了
            result.success = false;
            result.error_message = "Some tasks failed, blocking downstream tasks";
            result.status = WorkflowStatus::FAILED;
            running_ = false;
            return result;
        }
        
        // 批量执行任务
        auto batch_results = execute_batch_tasks(ready_tasks, task_results_);
        
        // 处理结果
        for (const auto& [task_id, task_result] : batch_results) {
            task_results_[task_id] = task_result;
            completed_tasks_.insert(task_id);
            
            if (!task_result.success) {
                if (error_strategy_ == ErrorHandlingStrategy::FAIL_FAST) {
                    result.success = false;
                    result.error_message = "Task failed: " + task_id + " - " + task_result.error_message;
                    result.status = WorkflowStatus::FAILED;
                    running_ = false;
                    return result;
                }
            }
        }
    }
    
    // 所有任务完成
    result.success = true;
    result.task_results = task_results_;
    result.status = WorkflowStatus::SUCCESS;
    result.total_tasks = dag_.size();
    result.completed_tasks = completed_tasks_.size();
    result.failed_tasks = 0;  // 如果到这里说明所有任务都成功了
    running_ = false;
    
    return result;
}

std::future<WorkflowResult> WorkflowScheduler::run_async() {
    std::promise<WorkflowResult> promise;
    auto future = promise.get_future();
    
    // 使用线程池的异步执行
    auto pool = executor_->thread_pool();
    if (pool) {
        pool->submit([this, promise = std::move(promise)]() mutable {
            try {
                auto result = run();
                promise.set_value(std::move(result));
            } catch (...) {
                promise.set_exception(std::current_exception());
            }
        });
    } else {
        // 回退到 std::async
        promise.set_value(run());
    }
    
    return future;
}

void WorkflowScheduler::pause() {
    paused_ = true;
}

void WorkflowScheduler::resume() {
    paused_ = false;
}

void WorkflowScheduler::cancel() {
    cancelled_ = true;
}

TaskResult WorkflowScheduler::execute_single_task(
    const TaskId& task_id,
    const std::map<TaskId, TaskResult>& completed_results) {
    
    auto task = dag_.get_task(task_id);
    if (!task) {
        return TaskResult::error("Task not found: " + task_id);
    }
    
    // 构建上下文
    TaskContext ctx;
    ctx.task_id = task_id;
    ctx.upstream_results = completed_results;
    
    // 执行任务
    return executor_->execute_task(task, ctx);
}

std::vector<std::pair<TaskId, TaskResult>> WorkflowScheduler::execute_batch_tasks(
    const std::vector<TaskId>& task_ids,
    const std::map<TaskId, TaskResult>& completed_results) {
    
    std::vector<std::pair<TaskId, TaskResult>> results;
    results.reserve(task_ids.size());
    
    // 准备任务列表
    std::vector<TaskPtr> tasks;
    std::map<TaskPtr, TaskId> task_id_map;
    
    for (const auto& task_id : task_ids) {
        auto task = dag_.get_task(task_id);
        if (task) {
            tasks.push_back(task);
            task_id_map[task] = task_id;
        }
    }
    
    // 构建上下文模板
    TaskContext ctx_template;
    ctx_template.upstream_results = completed_results;
    
    // 批量执行
    auto batch_results = executor_->execute_batch(tasks, ctx_template);
    
    // 组装结果
    for (size_t i = 0; i < tasks.size() && i < batch_results.size(); ++i) {
        results.emplace_back(task_id_map[tasks[i]], batch_results[i]);
    }
    
    return results;
}

bool WorkflowScheduler::should_stop() const {
    return cancelled_ || paused_;
}

WorkflowStatusSnapshot WorkflowScheduler::get_status() const {
    WorkflowStatusSnapshot snapshot;
    snapshot.running = running_.load();
    snapshot.total_tasks = dag_.size();
    snapshot.completed_tasks = completed_tasks_.size();
    
    // 统计失败任务数
    size_t failed_count = 0;
    for (const auto& [id, result] : task_results_) {
        if (!result.success) {
            failed_count++;
        }
    }
    snapshot.failed_tasks = failed_count;
    
    // 获取当前正在执行的任务（如果有）
    if (running_ && completed_tasks_.size() < dag_.size()) {
        auto ready_tasks = dag_.get_ready_tasks(completed_tasks_);
        if (!ready_tasks.empty()) {
            snapshot.current_task = ready_tasks[0];  // 返回第一个待执行任务
        }
    }
    
    return snapshot;
}

} // namespace workflow
} // namespace ben_gear
