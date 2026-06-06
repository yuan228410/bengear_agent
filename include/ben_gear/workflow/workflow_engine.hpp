#pragma once

#include "dag.hpp"
#include "executor.hpp"
#include "scheduler.hpp"
#include "storage.hpp"
#include "types.hpp"
#include "task_types.hpp"
#include "ben_gear/workspace/session.hpp"
#include <memory>
#include <map>
#include <shared_mutex>

namespace ben_gear {
namespace agent {
    class SharedResources;  // 前向声明
    class AgentCallbacks;   // 前向声明
}

namespace workflow {

/// 任务定义
struct WorkflowTaskDefinition {
    std::string id;
    std::string name;
    std::string type;  // llm / tool / function
    std::string prompt;  // LLM 提示词或工具调用
    std::vector<std::string> depends_on;
    Json config;
};

/// 工作流定义
struct WorkflowDefinition {
    std::string id;
    std::string name;
    std::vector<WorkflowTaskDefinition> tasks;
    Json variables;
    std::string on_failure = "abort";  // abort / continue / rollback
};

/// 工作流引擎（增强版）
class WorkflowEngine {
public:
    /// 构造函数
    explicit WorkflowEngine(
        std::shared_ptr<agent::SharedResources> resources,
        std::shared_ptr<base::concurrency::ThreadPool> thread_pool = nullptr);
    
    /// 注册工作流定义
    void register_workflow(const WorkflowDefinition& workflow);
    
    /// 验证工作流定义
    struct ValidationResult {
        bool valid;
        std::string error;
    };
    ValidationResult validate_workflow(const WorkflowDefinition& workflow);
    
    /// 执行工作流（同步）
    WorkflowState execute(
        const std::string& workflow_id);
    
    /// 暂停工作流
    bool pause(const std::string& execution_id);
    
    /// 恢复工作流
    bool resume(const std::string& execution_id);
    
    /// 取消工作流
    bool cancel(const std::string& execution_id);
    
    /// 获取工作流状态
    std::optional<WorkflowState> get_state(const std::string& execution_id) const;
    
    /// 获取工作流定义
    std::optional<WorkflowDefinition> get_workflow(const std::string& workflow_id) const;
    
    /// 动态添加任务
    bool add_task(
        const std::string& execution_id,
        const WorkflowTaskDefinition& task,
        const std::string& after_task = ""
    );
    
    /// 设置错误处理策略
    void set_error_strategy(ErrorHandlingStrategy strategy) {
        error_strategy_ = strategy;
    }
    
    /// 设置重试策略
    void set_retry_policy(const RetryPolicy& policy) {
        retry_policy_ = policy;
    }
    
private:
    /// 构建 DAG
    DAG build_dag(const WorkflowDefinition& workflow);
    
    /// 创建任务
    TaskPtr create_task(
        const WorkflowTaskDefinition& task_def,
        const WorkflowDefinition& workflow);
    
    /// 生成执行 ID
    std::string generate_execution_id();
    
private:
    std::shared_ptr<agent::SharedResources> resources_;
    std::shared_ptr<TaskExecutor> executor_;
    std::shared_ptr<IWorkflowStorage> storage_;
    
    std::map<std::string, WorkflowDefinition> workflows_;
    std::map<std::string, std::shared_ptr<WorkflowScheduler>> active_schedulers_;
    std::map<std::string, WorkflowState> running_workflows_;
    
    ErrorHandlingStrategy error_strategy_ = ErrorHandlingStrategy::FAIL_FAST;
    RetryPolicy retry_policy_;
    
    mutable std::shared_mutex mutex_;
};

} // namespace workflow
} // namespace ben_gear
