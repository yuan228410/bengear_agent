#pragma once

#include "dag.hpp"
#include "task.hpp"
#include "types.hpp"
#include <functional>
#include <string>

namespace ben_gear {
namespace workflow {

// 工作流构建器
class WorkflowBuilder {
public:
    WorkflowBuilder() = default;
    
    // 添加任务（函数形式）
    WorkflowBuilder& add_task(
        const TaskId& id, 
        FunctionTask::TaskFunc func);
    
    // 添加任务（便捷形式：id, agent, prompt, timeout）
    WorkflowBuilder& add_task(
        const TaskId& id,
        const std::string& agent,
        const std::string& prompt,
        int timeout = 600);
    
    // 添加任务（任务对象形式）
    WorkflowBuilder& add_task(TaskPtr task);
    
    // 添加依赖关系（to 依赖 from）
    WorkflowBuilder& add_dependency(const TaskId& from, const TaskId& to);
    
    // 设置错误处理策略
    WorkflowBuilder& set_error_strategy(ErrorHandlingStrategy strategy);
    
    // 设置重试策略
    WorkflowBuilder& set_retry_policy(const RetryPolicy& policy);
    
    // 设置工作流名称
    WorkflowBuilder& set_name(const std::string& name);
    
    // 构建工作流
    DAG build() const;
    
    // 清空构建器
    void clear();
    
    // 获取任务数量
    size_t task_count() const;
    
private:
    DAG dag_;
    ErrorHandlingStrategy error_strategy_ = ErrorHandlingStrategy::FAIL_FAST;
    RetryPolicy retry_policy_;
    std::string name_;
};

} // namespace workflow
} // namespace ben_gear
