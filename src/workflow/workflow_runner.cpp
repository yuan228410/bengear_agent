#include "ben_gear/workflow/workflow_runner.hpp"

namespace ben_gear {
namespace workflow {

WorkflowRunner::WorkflowRunner(std::shared_ptr<base::concurrency::ThreadPool> thread_pool)
    : executor_(std::make_shared<TaskExecutor>(std::move(thread_pool)))
    , storage_(std::make_shared<MemoryStorage>()) {
}

WorkflowRunner::WorkflowRunner(
    std::shared_ptr<TaskExecutor> executor,
    std::shared_ptr<IWorkflowStorage> storage)
    : executor_(std::move(executor))
    , storage_(std::move(storage)) {
}

WorkflowResult WorkflowRunner::run(const DAG& dag) {
    auto scheduler = std::make_shared<WorkflowScheduler>(
        dag, executor_, error_strategy_);
    
    return scheduler->run();
}

std::future<WorkflowResult> WorkflowRunner::run_async(const DAG& dag) {
    return std::async(std::launch::async, [this, dag] { return run(dag); });
}

WorkflowResult WorkflowRunner::run_with_persistence(
    const DAG& dag, 
    const WorkflowId& workflow_id) {
    
    // 检查是否已存在
    if (storage_->exists(workflow_id)) {
        // 恢复执行
        return resume(workflow_id);
    }
    
    // 创建新的工作流状态
    WorkflowState state;
    state.id = workflow_id;
    state.status = WorkflowStatus::PENDING;
    
    // 保存初始状态
    storage_->save_workflow(workflow_id, state);
    
    // 执行工作流
    auto result = run(dag);
    
    // 更新状态
    state.status = result.status;
    state.task_results = result.task_results;
    
    storage_->save_workflow(workflow_id, state);
    
    return result;
}

WorkflowResult WorkflowRunner::resume(const WorkflowId& workflow_id) {
    auto state_opt = storage_->load_workflow(workflow_id);
    
    if (!state_opt) {
        return WorkflowResult{false, "Workflow not found: " + workflow_id, {}, WorkflowStatus::FAILED};
    }
    
    auto state = *state_opt;
    
    // 检查状态
    if (state.status == WorkflowStatus::SUCCESS || 
        state.status == WorkflowStatus::CANCELLED) {
        // 已完成或已取消，直接返回
        WorkflowResult result;
        result.success = (state.status == WorkflowStatus::SUCCESS);
        result.status = state.status;
        result.task_results = state.task_results;
        return result;
    }
    
    // TODO: 实现恢复逻辑
    // 需要根据已完成的任务重新构建 DAG，只执行未完成的任务
    
    return WorkflowResult{false, "Resume not implemented yet", {}, WorkflowStatus::FAILED};
}

void WorkflowRunner::cancel(const WorkflowId& workflow_id) {
    auto it = active_schedulers_.find(workflow_id);
    if (it != active_schedulers_.end()) {
        it->second->cancel();
    }
}

std::optional<WorkflowState> WorkflowRunner::get_workflow_state(const WorkflowId& workflow_id) {
    return storage_->load_workflow(workflow_id);
}

void WorkflowRunner::delete_workflow(const WorkflowId& workflow_id) {
    storage_->delete_workflow(workflow_id);
    active_schedulers_.erase(workflow_id);
}

void WorkflowRunner::set_error_strategy(ErrorHandlingStrategy strategy) {
    error_strategy_ = strategy;
}

void WorkflowRunner::set_retry_policy(const RetryPolicy& policy) {
    retry_policy_ = policy;
}

WorkflowStatusSnapshot WorkflowRunner::get_status() const {
    WorkflowStatusSnapshot snapshot;
    
    // 从活跃的调度器中获取状态
    for (const auto& [id, scheduler] : active_schedulers_) {
        if (scheduler) {
            auto status = scheduler->get_status();
            snapshot.running = status.running;
            snapshot.total_tasks = status.total_tasks;
            snapshot.completed_tasks = status.completed_tasks;
            snapshot.failed_tasks = status.failed_tasks;
            snapshot.current_task = status.current_task;
            break;  // 只返回第一个活跃工作流的状态
        }
    }
    
    return snapshot;
}

} // namespace workflow
} // namespace ben_gear
