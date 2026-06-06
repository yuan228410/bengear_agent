#include "ben_gear/workflow/workflow_builder.hpp"

namespace ben_gear {
namespace workflow {

WorkflowBuilder& WorkflowBuilder::add_task(
    const TaskId& id, 
    FunctionTask::TaskFunc func) {
    
    auto task = TaskFactory::create_function_task(id, std::move(func));
    dag_.add_task(id, task);
    return *this;
}

WorkflowBuilder& WorkflowBuilder::add_task(
    const TaskId& id,
    const std::string& agent,
    const std::string& prompt,
    int timeout) {
    
    // 创建一个简单的任务函数（实际执行时由 WorkflowRunner 处理）
    // 注意：需要捕获 id 的副本，因为 id 是引用
    std::string task_id = id;
    auto func = [agent, prompt, timeout, task_id](const TaskContext& ctx) -> TaskResult {
        // 这里只是占位，实际执行逻辑在 WorkflowRunner 中
        TaskResult result;
        result.success = true;
        result.output = "Task " + task_id + " configured with agent=" + agent;
        return result;
    };
    
    auto task = TaskFactory::create_function_task(id, std::move(func));
    dag_.add_task(id, task);
    return *this;
}

WorkflowBuilder& WorkflowBuilder::add_task(TaskPtr task) {
    if (task) {
        dag_.add_task(task->id(), task);
    }
    return *this;
}

WorkflowBuilder& WorkflowBuilder::add_dependency(const TaskId& from, const TaskId& to) {
    dag_.add_dependency(from, to);
    return *this;
}

WorkflowBuilder& WorkflowBuilder::set_error_strategy(ErrorHandlingStrategy strategy) {
    error_strategy_ = strategy;
    return *this;
}

WorkflowBuilder& WorkflowBuilder::set_retry_policy(const RetryPolicy& policy) {
    retry_policy_ = policy;
    return *this;
}

WorkflowBuilder& WorkflowBuilder::set_name(const std::string& name) {
    name_ = name;
    return *this;
}

DAG WorkflowBuilder::build() const {
    return dag_;  // 返回副本
}

void WorkflowBuilder::clear() {
    dag_ = DAG();
    error_strategy_ = ErrorHandlingStrategy::FAIL_FAST;
    retry_policy_ = RetryPolicy{};
    name_.clear();
}

size_t WorkflowBuilder::task_count() const {
    return dag_.size();
}

} // namespace workflow
} // namespace ben_gear
