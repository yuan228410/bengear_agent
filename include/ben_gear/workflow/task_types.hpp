#pragma once

#include "task.hpp"
#include "types.hpp"
#include "ben_gear/base/utils/json.hpp"
#include <memory>

namespace ben_gear {
namespace agent {
    class Agent;        // 前向声明
    class SharedResources;  // 前向声明
}

namespace llm {
    class ToolRegistry;  // 前向声明
}

namespace workflow {

/// LLM 任务配置
struct LLMTaskConfig {
    std::string prompt;                    // 提示词（支持变量替换）
    std::string model;                     // 模型名称（可选，使用默认）
    int timeout_seconds = 600;             // 超时时间
    bool stream = false;                   // 是否流式输出
};

/// LLM 任务（调用 Agent 执行）
class LLMTask : public ITask {
public:
    LLMTask(
        const TaskId& id,
        std::shared_ptr<agent::Agent> agent,
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
    std::shared_ptr<agent::Agent> agent_;
    LLMTaskConfig config_;
    TaskStatus status_;
};

/// Tool 任务配置
struct ToolTaskConfig {
    std::string tool_name;                 // 工具名称
    Json arguments;                        // 工具参数（支持变量替换）
    int timeout_seconds = 60;              // 超时时间
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

/// 任务工厂扩展
class TaskFactoryEx {
public:
    /// 创建 LLM 任务
    static TaskPtr create_llm_task(
        const TaskId& id,
        std::shared_ptr<agent::Agent> agent,
        const LLMTaskConfig& config);
    
    /// 创建 Tool 任务
    static TaskPtr create_tool_task(
        const TaskId& id,
        std::shared_ptr<llm::ToolRegistry> registry,
        const ToolTaskConfig& config);
};

} // namespace workflow
} // namespace ben_gear
