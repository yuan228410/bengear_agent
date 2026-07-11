#pragma once

#include "task.hpp"
#include "types.hpp"
#include "workflow_resources.hpp"
#include "human_approval.hpp"
#include <memory>

namespace ben_gear {
namespace llm {
    class ToolRegistry;  // 前向声明
}

namespace workflow {

/// LLM 任务配置
struct LLMTaskConfig {
    std::string prompt;                    // 提示词（支持变量替换）
    std::string model;                     // 模型名称（可选，使用默认）
    int timeout_seconds = 0;                // 超时时间（秒），0=使用 Settings.workflow.task_timeout
    bool stream = false;                   // 是否流式输出
};

/// LLM 任务（通过 WorkflowResources 执行，不依赖 Agent）
class LLMTask : public ITask {
public:
    LLMTask(
        const TaskId& id,
        WorkflowResources resources,
        const LLMTaskConfig& config);
    
    /// 同步执行（阻塞）
    TaskResult execute(const TaskContext& ctx) override;
    
    TaskId id() const override { return id_; }
    TaskStatus status() const override { return status_; }
    void set_status(TaskStatus status) override { status_ = status; }
    
private:
    /// 解析变量（替换 {task_id} 为上游结果）
    std::string resolve_variables(const std::string& prompt, const TaskContext& ctx);
    std::string replace_all(const std::string& str, const std::string& from, const std::string& to);
    
private:
    TaskId id_;
    WorkflowResources resources_;
    LLMTaskConfig config_;
    TaskStatus status_;
};

/// Tool 任务配置
struct ToolTaskConfig {
    std::string tool_name;                 // 工具名称
    Json arguments;                        // 工具参数（支持变量替换）
    int timeout_seconds = 0;               // 超时时间（秒），0=使用 Settings.workflow.task_timeout
};

/// Tool 任务（执行工具）
class ToolTask : public ITask {
public:
    ToolTask(
        const TaskId& id,
        std::shared_ptr<llm::ToolRegistry> registry,
        const ToolTaskConfig& config);
    
    /// 同步执行
    TaskResult execute(const TaskContext& ctx) override;
    
    TaskId id() const override { return id_; }
    TaskStatus status() const override { return status_; }
    void set_status(TaskStatus status) override { status_ = status; }
    
private:
    Json resolve_arguments(const Json& args, const TaskContext& ctx);
    void resolve_json_variables(Json& json, const TaskContext& ctx);
    std::string resolve_variables(const std::string& str, const TaskContext& ctx);
    std::string replace_all(const std::string& str, const std::string& from, const std::string& to);
    
private:
    TaskId id_;
    std::shared_ptr<llm::ToolRegistry> registry_;
    ToolTaskConfig config_;
    TaskStatus status_;
};

/// 条件任务配置
struct ConditionTaskConfig {
    std::string expression;
    bool default_value = false;
};

/// 条件任务：输出 true/false，不再静默降级 function。
class ConditionTask : public ITask {
public:
    ConditionTask(const TaskId& id, const ConditionTaskConfig& config);
    TaskResult execute(const TaskContext& ctx) override;
    TaskId id() const override { return id_; }
    TaskStatus status() const override { return status_; }
    void set_status(TaskStatus status) override { status_ = status; }

private:
    bool evaluate(const TaskContext& ctx) const;

    TaskId id_;
    ConditionTaskConfig config_;
    TaskStatus status_;
};

/// 子工作流任务配置
struct SubflowTaskConfig {
    std::string workflow_id;
};

/// 子工作流任务
class SubflowTask : public ITask {
public:
    SubflowTask(const TaskId& id, WorkflowResources resources, const SubflowTaskConfig& config);
    TaskResult execute(const TaskContext& ctx) override;
    TaskId id() const override { return id_; }
    TaskStatus status() const override { return status_; }
    void set_status(TaskStatus status) override { status_ = status; }

private:
    TaskId id_;
    WorkflowResources resources_;
    SubflowTaskConfig config_;
    TaskStatus status_;
};

/// Sub-agent 任务配置。通过 ToolRegistry 调用 delegate_task，保持 workflow 不依赖 agent 层。
struct SubAgentTaskConfig {
    std::string prompt;
    std::string model_override;
    Json arguments;
};

class SubAgentWorkflowTask : public ITask {
public:
    SubAgentWorkflowTask(const TaskId& id,
                         std::shared_ptr<llm::ToolRegistry> registry,
                         const SubAgentTaskConfig& config);
    TaskResult execute(const TaskContext& ctx) override;
    TaskId id() const override { return id_; }
    TaskStatus status() const override { return status_; }
    void set_status(TaskStatus status) override { status_ = status; }

private:
    TaskId id_;
    std::shared_ptr<llm::ToolRegistry> registry_;
    SubAgentTaskConfig config_;
    TaskStatus status_;
};

/// 任务工厂扩展
class TaskFactoryEx {
public:
    /// 创建 LLM 任务
    static TaskPtr create_llm_task(
        const TaskId& id,
        WorkflowResources resources,
        const LLMTaskConfig& config);
    
    /// 创建 Tool 任务
    static TaskPtr create_tool_task(
        const TaskId& id,
        std::shared_ptr<llm::ToolRegistry> registry,
        const ToolTaskConfig& config);

    static TaskPtr create_condition_task(
        const TaskId& id,
        const ConditionTaskConfig& config);

    static TaskPtr create_subflow_task(
        const TaskId& id,
        WorkflowResources resources,
        const SubflowTaskConfig& config);

    static TaskPtr create_approval_task(
        const TaskId& id,
        const HumanApprovalConfig& config);

    static TaskPtr create_sub_agent_task(
        const TaskId& id,
        std::shared_ptr<llm::ToolRegistry> registry,
        const SubAgentTaskConfig& config);
};

} // namespace workflow
} // namespace ben_gear
