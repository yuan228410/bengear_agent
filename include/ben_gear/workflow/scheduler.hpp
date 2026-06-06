#pragma once

#include "dag.hpp"
#include "executor.hpp"
#include "types.hpp"
#include <atomic>
#include <future>
#include <unordered_set>
#include <mutex>

namespace ben_gear {
namespace workflow {

// 工作流调度器
class WorkflowScheduler {
public:
    explicit WorkflowScheduler(
        DAG dag, 
        std::shared_ptr<TaskExecutor> executor,
        ErrorHandlingStrategy error_strategy = ErrorHandlingStrategy::FAIL_FAST);
    
    // 运行工作流（同步）
    WorkflowResult run();
    
    // 运行工作流（异步）
    std::future<WorkflowResult> run_async();
    
    // 暂停工作流
    void pause();
    
    // 恢复工作流
    void resume();
    
    // 取消工作流
    void cancel();
    
    // 是否正在运行
    bool is_running() const { return running_; }
    
    // 是否已暂停
    bool is_paused() const { return paused_; }
    
    // 是否已取消
    bool is_cancelled() const { return cancelled_; }
    
    // 获取状态快照
    WorkflowStatusSnapshot get_status() const;
    
private:
    // 执行单个任务
    TaskResult execute_single_task(
        const TaskId& task_id,
        const std::map<TaskId, TaskResult>& completed_results);
    
    // 批量执行任务
    std::vector<std::pair<TaskId, TaskResult>> execute_batch_tasks(
        const std::vector<TaskId>& task_ids,
        const std::map<TaskId, TaskResult>& completed_results);
    
    // 检查是否应该停止
    bool should_stop() const;
    
private:
    DAG dag_;
    std::shared_ptr<TaskExecutor> executor_;
    ErrorHandlingStrategy error_strategy_;
    
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> cancelled_{false};
    
    mutable std::mutex mutex_;
    std::unordered_set<TaskId> completed_tasks_;
    std::map<TaskId, TaskResult> task_results_;
};

} // namespace workflow
} // namespace ben_gear
