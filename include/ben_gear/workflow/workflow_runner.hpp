#pragma once

#include "dag.hpp"
#include "executor.hpp"
#include "scheduler.hpp"
#include "storage.hpp"
#include "types.hpp"
#include <memory>
#include <string>

namespace ben_gear {
namespace workflow {

// 工作流运行器
class WorkflowRunner {
public:
    // 使用共享的线程池（推荐）
    explicit WorkflowRunner(std::shared_ptr<base::concurrency::ThreadPool> thread_pool);
    
    // 自定义执行器和存储
    WorkflowRunner(
        std::shared_ptr<TaskExecutor> executor,
        std::shared_ptr<IWorkflowStorage> storage);
    
    // 运行工作流（同步）
    WorkflowResult run(const DAG& dag);
    
    // 运行工作流（异步）
    std::future<WorkflowResult> run_async(const DAG& dag);
    
    // 运行工作流并持久化
    WorkflowResult run_with_persistence(
        const DAG& dag, 
        const WorkflowId& workflow_id);
    
    // 恢复工作流
    WorkflowResult resume(const WorkflowId& workflow_id);
    
    // 取消工作流
    void cancel(const WorkflowId& workflow_id);
    
    // 获取工作流状态
    std::optional<WorkflowState> get_workflow_state(const WorkflowId& workflow_id);
    
    // 获取最近工作流状态快照
    WorkflowStatusSnapshot get_status() const;
    
    // 删除工作流
    void delete_workflow(const WorkflowId& workflow_id);
    
    // 设置错误处理策略
    void set_error_strategy(ErrorHandlingStrategy strategy);
    
    // 设置重试策略
    void set_retry_policy(const RetryPolicy& policy);
    
private:
    std::shared_ptr<TaskExecutor> executor_;
    std::shared_ptr<IWorkflowStorage> storage_;
    ErrorHandlingStrategy error_strategy_ = ErrorHandlingStrategy::FAIL_FAST;
    RetryPolicy retry_policy_;
    
    // 活跃的调度器（用于取消和恢复）
    std::map<WorkflowId, std::shared_ptr<WorkflowScheduler>> active_schedulers_;
};

} // namespace workflow
} // namespace ben_gear
