#include "ben_gear/workflow/task_types.hpp"
#include "ben_gear/agent/agent.hpp"
#include "ben_gear/tool/registry.hpp"
#include "ben_gear/workspace/session.hpp"
#include "ben_gear/base/net/event_loop.hpp"
#include "ben_gear/base/log/logger.hpp"
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
    , status_(TaskStatus::PENDING) {
    log::debug_fmt("LLMTask created: id={}, prompt_len={}", id_, config_.prompt.size());
}

TaskResult LLMTask::execute(const TaskContext& ctx) {
    set_status(TaskStatus::RUNNING);
    
    // 设置工作流任务追踪标签
    auto saved_trace = log::get_trace_id();
    std::string wf_trace = saved_trace.empty() ? "global" : saved_trace;
    wf_trace += ":wf:" + id_;
    log::set_trace_id(wf_trace);
    log::info_fmt("LLMTask execute start: id={}, upstream_count={}", id_, ctx.upstream_results.size());
    
    try {
        // 解析变量
        std::string resolved_prompt = resolve_variables(config_.prompt, ctx);
        log::debug_fmt("LLMTask prompt resolved: id={}, prompt_len={}", id_, resolved_prompt.size());
        
        // 使用 SharedResources 的工作流 IoContext（长驻 EventLoop，不创建局部 loop）
        auto& wf_loop = agent_->resources()->wf_context()->loop();
        
        // 创建临时 Session
        workspace::SessionConfig session_config;
        session_config.session_id = id_;
        auto deps = agent_->resources()->make_session_deps();
        workspace::Session session(session_config, deps, agent_->resources()->tools_mut());
        
        // 执行：通过 sync_wait 在工作流 EventLoop 线程上运行协程
        agent::NullAgentCallbacks callbacks;
        auto result = net::sync_wait(wf_loop, agent_->run_session_async(
            wf_loop, session, 
            base::container::String(resolved_prompt.c_str()),
            callbacks
        ));
        
        // 转换结果
        if (result.status == 200) {
            set_status(TaskStatus::SUCCESS);
            log::info_fmt("LLMTask execute success: id={}, text_len={}", id_, result.text.size());
            log::set_trace_id(saved_trace);
            return TaskResult::ok(std::move(result.text));
        } else {
            set_status(TaskStatus::FAILED);
            log::error_fmt("LLMTask execute failed: id={}, status={}", id_, result.status);
            log::set_trace_id(saved_trace);
            return TaskResult::error("LLM request failed with status " + std::to_string(result.status));
        }
    } catch (const std::exception& e) {
        set_status(TaskStatus::FAILED);
        log::error_fmt("LLMTask execute exception: id={}, error={}", id_, e.what());
        log::set_trace_id(saved_trace);
        return TaskResult::error(e.what());
    }
}

/// 从上游任务结果中提取文本输出（统一 base::container::String 类型）
static std::string extract_output_text(const TaskResult& task_result) {
    try {
        const auto& val = std::any_cast<const base::container::String&>(task_result.output);
        return std::string(val.data(), val.size());
    } catch (const std::bad_any_cast&) {
        log::warn_fmt("extract_output_text: unknown output type={}", task_result.output.type().name());
        return "";
    }
}

std::string LLMTask::resolve_variables(const std::string& prompt, const TaskContext& ctx) {
    std::string result = prompt;
    
    // 替换上游任务结果，兼容 {task_id} 和 {{task_id}} 两种格式
    for (const auto& [task_id, task_result] : ctx.upstream_results) {
        if (!task_result.success) continue;
        std::string output = extract_output_text(task_result);
        if (output.empty()) continue;
        log::debug_fmt("LLMTask resolve_variables: replace task_id={}, output_len={}", task_id, output.size());
        // 按长度从长到短替换，避免短模式先匹配破坏长模式
        // {{task_id.result}} 格式（带 .result 后缀，最长）
        std::string dot_placeholder = "{{" + task_id + ".result}}";
        result = replace_all(result, dot_placeholder, output);
        // {{task_id}} 格式（Mustache 风格，LLM 常用）
        std::string double_placeholder = "{{" + task_id + "}}";
        result = replace_all(result, double_placeholder, output);
        // {task_id} 格式（最短）
        std::string single_placeholder = "{" + task_id + "}";
        result = replace_all(result, single_placeholder, output);
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
    , status_(TaskStatus::PENDING) {
    // 创建时校验工具是否存在，避免执行时才报错
    if (registry_ && !registry_->has_tool(config_.tool_name)) {
        log::error_fmt("ToolTask created with unknown tool: id={}, tool={}. "
                       "Available tools can be listed via list_skills/list_workflow_templates",
                       id_, config_.tool_name);
    }
    log::debug_fmt("ToolTask created: id={}, tool={}, timeout={}s", id_, config_.tool_name, config_.timeout_seconds);
}

TaskResult ToolTask::execute(const TaskContext& ctx) {
    set_status(TaskStatus::RUNNING);
    
    // 设置工作流任务追踪标签
    auto saved_trace = log::get_trace_id();
    std::string wf_trace = saved_trace.empty() ? "global" : saved_trace;
    wf_trace += ":wf:" + id_;
    log::set_trace_id(wf_trace);
    log::info_fmt("ToolTask execute start: id={}, tool={}", id_, config_.tool_name);
    
    try {
        // 解析参数中的变量
        Json resolved_args = resolve_arguments(config_.arguments, ctx);
        log::debug_fmt("ToolTask args resolved: id={}, tool={}, args_len={}", id_, config_.tool_name, resolved_args.dump().size());
        
        // 执行工具
        auto result = registry_->execute(
            base::container::String(config_.tool_name.c_str()),
            resolved_args
        );
        
        if (result.success) {
            set_status(TaskStatus::SUCCESS);
            log::info_fmt("ToolTask execute success: id={}, tool={}, output_len={}", id_, config_.tool_name, result.output.size());
            log::set_trace_id(saved_trace);
            return TaskResult::ok(std::move(result.output));
        } else {
            set_status(TaskStatus::FAILED);
            log::error_fmt("ToolTask execute failed: id={}, tool={}, error={}", id_, config_.tool_name, result.error);
            log::set_trace_id(saved_trace);
            return TaskResult::error(result.error);
        }
    } catch (const std::exception& e) {
        set_status(TaskStatus::FAILED);
        log::error_fmt("ToolTask execute exception: id={}, tool={}, error={}", id_, config_.tool_name, e.what());
        log::set_trace_id(saved_trace);
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
        // Collect keys first to avoid iterator invalidation
        std::vector<base::container::String> keys;
        for (auto it = json.begin(); it != json.end(); ++it) {
            keys.push_back(it.key());
        }
        for (const auto& k : keys) {
            Json val = json[k];
            resolve_json_variables(val, ctx);
            json.set(k, val);
        }
    } else if (json.is_array()) {
        // Array: re-parse each element
        for (size_t i = 0; i < json.size(); ++i) {
            Json val = json[i];
            resolve_json_variables(val, ctx);
            // Note: array modification requires re-serialization
        }
    }
}

std::string ToolTask::resolve_variables(const std::string& str, const TaskContext& ctx) {
    std::string result = str;
    
    for (const auto& [task_id, task_result] : ctx.upstream_results) {
        if (!task_result.success) continue;
        std::string output = extract_output_text(task_result);
        if (output.empty()) continue;
        // 按长度从长到短替换，避免短模式先匹配破坏长模式
        result = replace_all(result, "{{" + task_id + "}}", output);
        result = replace_all(result, "{" + task_id + "}", output);
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
