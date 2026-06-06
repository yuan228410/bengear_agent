#include "ben_gear/workflow/task_types.hpp"
#include "ben_gear/agent/agent.hpp"
#include "ben_gear/tool/registry.hpp"
#include "ben_gear/workspace/session.hpp"
#include "ben_gear/base/net/event_loop.hpp"
#include <stdexcept>

namespace ben_gear {
namespace workflow {

// ==================== LLMTask 实现 ====================

LLMTask::LLMTask(
    const TaskId& id,
    std::shared_ptr<agent::Agent> agent,
    const LLMTaskConfig& config)
    : id_(id)
    , agent_(std::move(agent))
    , config_(config)
    , status_(TaskStatus::PENDING) {}

TaskResult LLMTask::execute(const TaskContext& ctx) {
    set_status(TaskStatus::RUNNING);
    
    try {
        // 解析变量
        std::string resolved_prompt = resolve_variables(config_.prompt, ctx);
        
        // 创建临时 EventLoop
        net::EventLoop loop;
        
        // 创建临时 Session
        workspace::SessionConfig session_config;
        session_config.session_id = id_;
        auto deps = agent_->resources()->make_session_deps();
        workspace::Session session(session_config, deps);
        
        // 执行
        agent::NullAgentCallbacks callbacks;
        auto result = loop.run(agent_->run_session_async(
            loop, session, 
            base::container::String(resolved_prompt.c_str()),
            callbacks
        ));
        
        // 转换结果
        if (result.status == 200) {
            set_status(TaskStatus::SUCCESS);
            return TaskResult::ok(std::move(result.text));
        } else {
            set_status(TaskStatus::FAILED);
            return TaskResult::error("LLM request failed with status " + std::to_string(result.status));
        }
    } catch (const std::exception& e) {
        set_status(TaskStatus::FAILED);
        return TaskResult::error(e.what());
    }
}

std::string LLMTask::resolve_variables(const std::string& prompt, const TaskContext& ctx) {
    std::string result = prompt;
    
    // 替换上游任务结果
    for (const auto& [task_id, task_result] : ctx.upstream_results) {
        std::string placeholder = "{" + task_id + "}";
        if (result.find(placeholder) != std::string::npos && task_result.success) {
            try {
                auto output = std::any_cast<std::string>(task_result.output);
                result = replace_all(result, placeholder, output);
            } catch (const std::bad_any_cast&) {
                // 忽略类型不匹配
            }
        }
    }
    
    return result;
}

std::string LLMTask::replace_all(const std::string& str, const std::string& from, const std::string& to) {
    std::string result = str;
    size_t pos = 0;
    while ((pos = result.find(from, pos)) != std::string::npos) {
        result.replace(pos, from.length(), to);
        pos += to.length();
    }
    return result;
}

// ==================== ToolTask 实现 ====================

ToolTask::ToolTask(
    const TaskId& id,
    std::shared_ptr<llm::ToolRegistry> registry,
    const ToolTaskConfig& config)
    : id_(id)
    , registry_(std::move(registry))
    , config_(config)
    , status_(TaskStatus::PENDING) {}

TaskResult ToolTask::execute(const TaskContext& ctx) {
    set_status(TaskStatus::RUNNING);
    
    try {
        // 解析参数中的变量
        Json resolved_args = resolve_arguments(config_.arguments, ctx);
        
        // 执行工具
        auto result = registry_->execute(
            base::container::String(config_.tool_name.c_str()),
            resolved_args
        );
        
        if (result.success) {
            set_status(TaskStatus::SUCCESS);
            return TaskResult::ok(std::string(result.output.data(), result.output.size()));
        } else {
            set_status(TaskStatus::FAILED);
            return TaskResult::error(result.error);
        }
    } catch (const std::exception& e) {
        set_status(TaskStatus::FAILED);
        return TaskResult::error(e.what());
    }
}

Json ToolTask::resolve_arguments(const Json& args, const TaskContext& ctx) {
    Json resolved = args;
    resolve_json_variables(resolved, ctx);
    return resolved;
}

void ToolTask::resolve_json_variables(Json& json, const TaskContext& ctx) {
    if (json.is_string()) {
        std::string str = json.get<std::string>();
        std::string resolved = resolve_variables(str, ctx);
        json = resolved;
    } else if (json.is_object()) {
        for (auto& [key, value] : json.items()) {
            resolve_json_variables(value, ctx);
        }
    } else if (json.is_array()) {
        for (auto& item : json) {
            resolve_json_variables(item, ctx);
        }
    }
}

std::string ToolTask::resolve_variables(const std::string& str, const TaskContext& ctx) {
    std::string result = str;
    
    for (const auto& [task_id, task_result] : ctx.upstream_results) {
        std::string placeholder = "{" + task_id + "}";
        if (result.find(placeholder) != std::string::npos && task_result.success) {
            try {
                auto output = std::any_cast<std::string>(task_result.output);
                result = replace_all(result, placeholder, output);
            } catch (const std::bad_any_cast&) {
                // 忽略
            }
        }
    }
    
    return result;
}

std::string ToolTask::replace_all(const std::string& str, const std::string& from, const std::string& to) {
    std::string result = str;
    size_t pos = 0;
    while ((pos = result.find(from, pos)) != std::string::npos) {
        result.replace(pos, from.length(), to);
        pos += to.length();
    }
    return result;
}

// ==================== TaskFactoryEx 实现 ====================

TaskPtr TaskFactoryEx::create_llm_task(
    const TaskId& id,
    std::shared_ptr<agent::Agent> agent,
    const LLMTaskConfig& config) {
    return std::make_shared<LLMTask>(id, std::move(agent), config);
}

TaskPtr TaskFactoryEx::create_tool_task(
    const TaskId& id,
    std::shared_ptr<llm::ToolRegistry> registry,
    const ToolTaskConfig& config) {
    return std::make_shared<ToolTask>(id, std::move(registry), config);
}

} // namespace workflow
} // namespace ben_gear
