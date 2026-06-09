#pragma once

#include "dag.hpp"
#include "executor.hpp"
#include "scheduler.hpp"
#include "storage.hpp"
#include "types.hpp"
#include "task_types.hpp"
#include "namespace.hpp"
#include "workflow_resources.hpp"
#include <memory>
#include <map>
#include <shared_mutex>

namespace ben_gear {
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
    /// @param resources 工作流资源（可选，可延迟绑定）
    /// @param thread_pool 线程池（可选，不传则使用 std::async，避免占用核心线程池）
    explicit WorkflowEngine(
        WorkflowResources resources = {},
        std::shared_ptr<base::concurrency::ThreadPool> thread_pool = nullptr);

    /// 延迟绑定资源（构造后由 SharedResources::post_init 调用）
    void bind_resources(WorkflowResources resources) {
        if (!resources.is_bound()) {
            log::error_fmt("workflow: bind_resources called with incomplete resources");
            return;
        }
        resources_ = std::move(resources);
        // 绑定时更新重试策略
        if (resources_.settings) {
            retry_policy_.max_retries = resources_.settings->workflow.max_retries;
            retry_policy_.retry_delay_ms = resources_.settings->workflow.retry_delay_ms;
        }
        log::info_fmt("workflow: resources bound successfully");
    }

    // --- 命名空间管理（委托给 namespace.hpp）---
    using NamespaceGuard = workflow::NamespaceGuard;

    static void set_current_namespace(const std::string& ns) {
        workflow::set_current_namespace(ns);
    }
    static const std::string& get_current_namespace() {
        return workflow::get_current_namespace();
    }
    static void clear_current_namespace() {
        workflow::clear_current_namespace();
    }

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
    /// 构建 DAG
    DAG build_dag(const WorkflowDefinition& workflow);
    
    /// 创建任务
    TaskPtr create_task(
        const WorkflowTaskDefinition& task_def,
        const WorkflowDefinition& workflow);
    
    /// 生成执行 ID
    std::string generate_execution_id();
    
private:
    WorkflowResources resources_;
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
