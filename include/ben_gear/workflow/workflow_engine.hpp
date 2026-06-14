#pragma once

#include "dag.hpp"
#include "executor.hpp"
#include "scheduler.hpp"
#include "storage.hpp"
#include "types.hpp"
#include "task_types.hpp"
#include "namespace.hpp"
#include "workflow_resources.hpp"
#include "metrics.hpp"
#include "ben_gear/base/log/logger.hpp"
#include <memory>
#include <map>
#include <shared_mutex>
#include <future>
#include <vector>

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
    explicit WorkflowEngine(
        WorkflowResources resources = {},
        std::shared_ptr<base::concurrency::ThreadPool> thread_pool = nullptr);

    void bind_resources(WorkflowResources resources) {
        if (!resources.is_bound()) {
            log::error_fmt("workflow: bind_resources called with incomplete resources");
            return;
        }
        resources_ = std::move(resources);
        if (resources_.settings) {
            retry_policy_.max_retries = resources_.settings->workflow.max_retries;
            retry_policy_.retry_delay_ms = resources_.settings->workflow.retry_delay_ms;
        }
        log::info_fmt("workflow: resources bound successfully");
    }

    // --- 命名空间管理 ---
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

    /// 注册工作流定义
    std::string register_workflow(const WorkflowDefinition& workflow,
                                  const std::string& ns = "");

    /// 验证工作流定义
    struct ValidationResult {
        bool valid;
        std::string error;
    };
    ValidationResult validate_workflow(const WorkflowDefinition& workflow);

    /// 执行工作流（同步）
    WorkflowState execute(const std::string& workflow_id);

    /// 启动异步工作流，立即返回 execution_id。
    /// 后续通过 get_state/pause/resume/cancel 使用 execution_id 控制。
    std::string start_async(const std::string& workflow_id);

    /// 执行工作流（异步，返回 future<WorkflowResult>）
    /// 低层 API；工具层优先使用 start_async 获取稳定 execution handle。
    std::future<WorkflowResult> execute_async(const std::string& workflow_id);

    /// 暂停工作流
    bool pause(const std::string& execution_id);

    /// 恢复工作流
    bool resume(const std::string& execution_id);

    /// 取消工作流
    bool cancel(const std::string& execution_id);

    /// 获取工作流状态
    std::optional<WorkflowState> get_state(const std::string& execution_id);

    /// 获取工作流定义
    std::optional<WorkflowDefinition> get_workflow(const std::string& workflow_id) const;

    /// 按命名空间列出工作流
    std::vector<std::string> list_workflows(const std::string& ns) const;

    /// 动态添加任务到工作流定义（仅限未运行的工作流）
    /// @param workflow_id 工作流 ID
    /// @param task 任务定义
    /// @param after_task 在指定任务之后添加（自动添加依赖），空则不自动添加
    bool add_task(const std::string& workflow_id,
                  const WorkflowTaskDefinition& task,
                  const std::string& after_task = "");

    /// 设置错误处理策略
    void set_error_strategy(ErrorHandlingStrategy strategy) {
        error_strategy_ = strategy;
    }

    /// 设置重试策略
    void set_retry_policy(const RetryPolicy& policy) {
        retry_policy_ = policy;
    }

    // ---- 可观测接口 ----

    /// 设置进度回调
    void set_progress_callbacks(std::shared_ptr<WorkflowProgressCallbacks> callbacks) {
        progress_callbacks_ = std::move(callbacks);
    }

    /// 获取指标收集器
    std::shared_ptr<MetricsCollector> metrics() { return metrics_; }
    const std::shared_ptr<MetricsCollector>& metrics() const { return metrics_; }

private:
    DAG build_dag(const WorkflowDefinition& workflow);
    TaskPtr create_task(const WorkflowTaskDefinition& task_def,
                        const WorkflowDefinition& workflow);
    std::string generate_execution_id();

private:
    WorkflowResources resources_;
    std::shared_ptr<TaskExecutor> executor_;
    std::shared_ptr<IWorkflowStorage> storage_;
    std::shared_ptr<WorkflowProgressCallbacks> progress_callbacks_;
    std::shared_ptr<MetricsCollector> metrics_;

    std::map<std::string, WorkflowDefinition> workflows_;
    std::map<std::string, std::shared_ptr<WorkflowScheduler>> active_schedulers_;
    std::map<std::string, WorkflowState> running_workflows_;
    std::map<std::string, std::future<WorkflowResult>> active_futures_;

    ErrorHandlingStrategy error_strategy_ = ErrorHandlingStrategy::FAIL_FAST;
    RetryPolicy retry_policy_;

    mutable std::shared_mutex mutex_;
};

} // namespace workflow
} // namespace ben_gear
