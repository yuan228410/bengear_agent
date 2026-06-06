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
        std::shared_ptr<agent::SharedResources> resources = nullptr);

    /// 延迟绑定 SharedResources（构造后由 SharedResources::post_init 调用）
    void bind_resources(std::shared_ptr<agent::SharedResources> resources) {
        resources_ = std::move(resources);
    }

    /// 设置当前线程的命名空间（Agent 执行工具前调用）
    static void set_current_namespace(const std::string& ns) { current_namespace() = ns; }
    /// 获取当前线程的命名空间
    static const std::string& get_current_namespace() { return current_namespace(); }
    /// 清除当前线程的命名空间
    static void clear_current_namespace() { current_namespace().clear(); }

/// 注册工作流定义
 /// 注册工作流定义，自动加命名空间前缀（username::workspace::session_id::workflow_id）
 /// 返回带前缀的 workflow_id
 std::string register_workflow(const WorkflowDefinition& workflow,
                               const std::string& ns = "");
    
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

 /// 按命名空间列出工作流
 std::vector<std::string> list_workflows(const std::string& ns) const;
    
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
    /// thread_local 命名空间存储
    static std::string& current_namespace() {
        static thread_local std::string ns;
        return ns;
    }

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
