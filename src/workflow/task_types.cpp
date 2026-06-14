#include "ben_gear/workflow/task_types.hpp"
#include "ben_gear/tool/registry.hpp"
#include "ben_gear/base/net/event_loop.hpp"
#include "ben_gear/base/log/logger.hpp"
#include <stdexcept>
#include <string_view>

namespace ben_gear {
namespace workflow {

// ==================== LLMTask 实现 ====================

LLMTask::LLMTask(
    const TaskId& id,
    WorkflowResources resources,
    const LLMTaskConfig& config)
    : id_(id)
    , resources_(std::move(resources))
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
    log::info_fmt("LLMTask execute start: id={}, upstream_count={}", id_, (ctx.upstream_results ? ctx.upstream_results->size() : 0));
    
    try {
        // 解析变量
        std::string resolved_prompt = resolve_variables(config_.prompt, ctx);
        log::debug_fmt("LLMTask prompt resolved: id={}, prompt_len={}", id_, resolved_prompt.size());
        
        // 使用 WorkflowResources 的工作流 IoContext（长驻 EventLoop）
        auto& wf_loop = resources_.wf_context->loop();
        
        // 通过函数绑定执行 LLM 会话（内部封装 Agent + Session 创建）
        auto result = net::sync_wait(wf_loop, resources_.run_chat_async(
            wf_loop, id_,
            base::container::String(resolved_prompt),
            base::container::String(config_.model)
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

static std::string delegate_failure_message(std::string_view output) {
    base::container::String parse_error;
    auto json = Json::parse(output, parse_error);
    if (!parse_error.empty() || !json.is_object() || !json.contains("success")) {
        return {};
    }
    if (json.value("success", true)) {
        return {};
    }
    if (json.contains("error")) {
        return json["error"].get<std::string>();
    }
    if (json.contains("text")) {
        return json["text"].get<std::string>();
    }
    return "Sub-agent task reported failure";
}

std::string LLMTask::resolve_variables(const std::string& prompt, const TaskContext& ctx) {
    std::string result = prompt;
    
    if (ctx.upstream_results) for (const auto& [task_id, task_result] : *ctx.upstream_results) {
        if (!task_result.success) continue;
        std::string output = extract_output_text(task_result);
        if (output.empty()) continue;
        log::debug_fmt("LLMTask resolve_variables: replace task_id={}, output_len={}", task_id, output.size());
        std::string dot_placeholder = "{{" + task_id + ".result}}";
        result = replace_all(result, dot_placeholder, output);
        std::string double_placeholder = "{{" + task_id + "}}";
        result = replace_all(result, double_placeholder, output);
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
    if (registry_ && !registry_->has_tool(config_.tool_name)) {
        log::error_fmt("ToolTask created with unknown tool: id={}, tool={}. "
                       "Available tools can be listed via list_skills/list_workflow_templates",
                       id_, config_.tool_name);
    }
    log::debug_fmt("ToolTask created: id={}, tool={}, timeout={}s", id_, config_.tool_name, config_.timeout_seconds);
}

TaskResult ToolTask::execute(const TaskContext& ctx) {
    set_status(TaskStatus::RUNNING);
    
    auto saved_trace = log::get_trace_id();
    std::string wf_trace = saved_trace.empty() ? "global" : saved_trace;
    wf_trace += ":wf:" + id_;
    log::set_trace_id(wf_trace);
    log::info_fmt("ToolTask execute start: id={}, tool={}", id_, config_.tool_name);
    
    try {
        Json resolved_args = resolve_arguments(config_.arguments, ctx);
        log::debug_fmt("ToolTask args resolved: id={}, tool={}, args_len={}", id_, config_.tool_name, resolved_args.dump().size());
        
        auto result = registry_->execute(
            base::container::String(config_.tool_name),
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
        for (size_t i = 0; i < json.size(); ++i) {
            Json val = json[i];
            resolve_json_variables(val, ctx);
        }
    }
}

std::string ToolTask::resolve_variables(const std::string& str, const TaskContext& ctx) {
    std::string result = str;
    
    if (ctx.upstream_results) for (const auto& [task_id, task_result] : *ctx.upstream_results) {
        if (!task_result.success) continue;
        std::string output = extract_output_text(task_result);
        if (output.empty()) continue;
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

// ==================== Condition/Subflow/SubAgent 任务实现 ====================

ConditionTask::ConditionTask(const TaskId& id, const ConditionTaskConfig& config)
    : id_(id), config_(config), status_(TaskStatus::PENDING) {}

bool ConditionTask::evaluate(const TaskContext& ctx) const {
    if (config_.expression == "true") return true;
    if (config_.expression == "false") return false;
    if (config_.expression.starts_with("success:")) {
        auto task_id = config_.expression.substr(std::string("success:").size());
        if (!ctx.upstream_results) return false;
        auto it = ctx.upstream_results->find(task_id);
        return it != ctx.upstream_results->end() && it->second.success;
    }
    if (config_.expression.starts_with("non_empty:")) {
        auto task_id = config_.expression.substr(std::string("non_empty:").size());
        if (!ctx.upstream_results) return false;
        auto it = ctx.upstream_results->find(task_id);
        if (it == ctx.upstream_results->end() || !it->second.success) return false;
        return !extract_output_text(it->second).empty();
    }
    return config_.default_value;
}

TaskResult ConditionTask::execute(const TaskContext& ctx) {
    set_status(TaskStatus::RUNNING);
    const bool value = evaluate(ctx);
    set_status(TaskStatus::SUCCESS);
    return TaskResult::ok(base::container::String(value ? "true" : "false"));
}

SubflowTask::SubflowTask(const TaskId& id, WorkflowResources resources, const SubflowTaskConfig& config)
    : id_(id), resources_(std::move(resources)), config_(config), status_(TaskStatus::PENDING) {}

TaskResult SubflowTask::execute(const TaskContext&) {
    set_status(TaskStatus::RUNNING);
    if (!resources_.tools) {
        set_status(TaskStatus::FAILED);
        return TaskResult::error("Subflow task failed: tools not bound");
    }
    Json args;
    args["workflow_id"] = config_.workflow_id;
    args["async"] = false;
    auto result = resources_.tools->execute(base::container::String("execute_workflow"), args);
    if (result.success) {
        set_status(TaskStatus::SUCCESS);
        return TaskResult::ok(std::move(result.output));
    }
    set_status(TaskStatus::FAILED);
    return TaskResult::error(result.error);
}

SubAgentWorkflowTask::SubAgentWorkflowTask(const TaskId& id,
                                           std::shared_ptr<llm::ToolRegistry> registry,
                                           const SubAgentTaskConfig& config)
    : id_(id), registry_(std::move(registry)), config_(config), status_(TaskStatus::PENDING) {}

TaskResult SubAgentWorkflowTask::execute(const TaskContext&) {
    set_status(TaskStatus::RUNNING);
    if (!registry_) {
        set_status(TaskStatus::FAILED);
        return TaskResult::error("Sub-agent task failed: tool registry not bound");
    }
    Json args = config_.arguments.is_object() ? config_.arguments : Json::object();
    if (!config_.prompt.empty() && !args.contains("prompt")) {
        args["prompt"] = config_.prompt;
    }
    if (!config_.model_override.empty() && !args.contains("model_override")) {
        args["model_override"] = config_.model_override;
    }
    auto result = registry_->execute(base::container::String("delegate_task"), args);
    if (result.success) {
        auto failure = delegate_failure_message(std::string_view(result.output.data(), result.output.size()));
        if (!failure.empty()) {
            set_status(TaskStatus::FAILED);
            return TaskResult::error(failure);
        }
        set_status(TaskStatus::SUCCESS);
        return TaskResult::ok(std::move(result.output));
    }
    set_status(TaskStatus::FAILED);
    return TaskResult::error(result.error);
}

// ==================== TaskFactoryEx 实现 ====================

TaskPtr TaskFactoryEx::create_llm_task(
    const TaskId& id,
    WorkflowResources resources,
    const LLMTaskConfig& config) {
    return std::make_shared<LLMTask>(id, std::move(resources), config);
}

TaskPtr TaskFactoryEx::create_tool_task(
    const TaskId& id,
    std::shared_ptr<llm::ToolRegistry> registry,
    const ToolTaskConfig& config) {
    return std::make_shared<ToolTask>(id, std::move(registry), config);
}

TaskPtr TaskFactoryEx::create_condition_task(
    const TaskId& id,
    const ConditionTaskConfig& config) {
    return std::make_shared<ConditionTask>(id, config);
}

TaskPtr TaskFactoryEx::create_subflow_task(
    const TaskId& id,
    WorkflowResources resources,
    const SubflowTaskConfig& config) {
    return std::make_shared<SubflowTask>(id, std::move(resources), config);
}

TaskPtr TaskFactoryEx::create_approval_task(
    const TaskId& id,
    const HumanApprovalConfig& config) {
    return std::make_shared<HumanApprovalTask>(id, config);
}

TaskPtr TaskFactoryEx::create_sub_agent_task(
    const TaskId& id,
    std::shared_ptr<llm::ToolRegistry> registry,
    const SubAgentTaskConfig& config) {
    return std::make_shared<SubAgentWorkflowTask>(id, std::move(registry), config);
}

} // namespace workflow
} // namespace ben_gear
