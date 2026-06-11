#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/vector.hpp"
#include "ben_gear/base/utils/json.hpp"
#include "ben_gear/base/log/logger.hpp"

#include <optional>
#include <string>
#include <vector>

namespace ben_gear::llm {

namespace container = base::container;

/// 工具参数 Schema
struct ToolParameterSchema {
    container::String type = container::String("string");
    container::String description;
    container::Vector<container::String> enum_values;
    bool required = true;
};

/// 工具定义（协议无关）
struct ToolDefinition {
    container::String name;
    container::String description;
    container::Vector<std::pair<container::String, ToolParameterSchema>> parameters;
    bool read_only = false;  // 只读工具标记：plan 模式下只允许只读工具

    /// 转换为 OpenAI 格式
    Json to_openai_format() const;

    /// 转换为 Anthropic 格式
    Json to_anthropic_format() const;
};

/// 工具调用请求（LLM 协议层）
struct ToolCallRequest {
    container::String id;
    container::String name;
    Json arguments = Json::object();

    /// 从 OpenAI 格式解析
    static ToolCallRequest from_openai(const Json& j);

    /// 从 Anthropic 格式解析
    static ToolCallRequest from_anthropic(const Json& j);

    /// 转换为 OpenAI 格式工具结果消息
    Json to_openai_tool_message(const container::String& result) const;

    /// 转换为 Anthropic 格式工具结果消息
    Json to_anthropic_tool_message(const container::String& result) const;

private:
    /// 清理 LLM 内部特殊 token 泄漏
    static void sanitize_model_tokens(std::string& json_str);
};

/// 工具执行结果
struct ToolResult {
    bool success = true;
    container::String output;
    container::String error;

    static ToolResult ok(container::String result) {
        return {true, std::move(result), {}};
    }
    static ToolResult not_found(std::string_view name) {
        container::String msg("tool not found: ");
        msg += name;
        return {false, {}, std::move(msg)};
    }
    static ToolResult execution_error(std::string_view name,
                                       std::string_view what) {
        container::String msg("tool '");
        msg += name;
        msg += "' failed: ";
        msg += what;
        return {false, {}, std::move(msg)};
    }
    static ToolResult unknown_error(std::string_view name) {
        container::String msg("tool '");
        msg += name;
        msg += "' failed: unknown exception";
        return {false, {}, std::move(msg)};
    }
};

/// 工具调用结果（LLM 协议层）
struct ToolCallResult {
    container::String tool_call_id;
    container::String name;
    container::String output;
    bool success = true;
};

/// 工具选择策略
enum class ToolChoice { auto_, none, required, specific };

/// 工具选择配置
struct ToolChoiceConfig {
    ToolChoice choice = ToolChoice::auto_;
    std::optional<container::String> tool_name;

    /// 转换为 OpenAI 格式
    Json to_openai_format() const {
        switch (choice) {
            case ToolChoice::auto_: return "auto";
            case ToolChoice::none: return "none";
            case ToolChoice::required: return "required";
            case ToolChoice::specific:
                return Json{{"type", "function"},
                            {"function", {{"name", *tool_name}}}};
        }
        return "auto";
    }

    /// 转换为 Anthropic 格式
    Json to_anthropic_format() const {
        switch (choice) {
            case ToolChoice::auto_: return Json{{"type", "auto"}};
            case ToolChoice::none: return Json{{"type", "any"}};
            case ToolChoice::required: return Json{{"type", "any"}};
            case ToolChoice::specific:
                return Json{{"type", "tool"}, {"name", *tool_name}};
        }
        return Json{{"type", "auto"}};
    }
};

/// 计划模式工具过滤结果
struct PlanFilterResult {
    std::vector<ToolCallRequest> allowed;          ///< 允许执行的工具调用
    std::vector<ToolCallRequest> blocked_calls;    ///< 被拦截的 tool_use（需加入 assistant 消息）
    std::vector<ToolCallResult> blocked_results;   ///< 被拦截的 tool_result（需回传 LLM）
};

}  // namespace ben_gear::llm

namespace ben_gear {
using ToolDefinition = llm::ToolDefinition;
using ToolCallRequest = llm::ToolCallRequest;
using ToolCallResult = llm::ToolCallResult;
using ToolChoice = llm::ToolChoice;
using ToolChoiceConfig = llm::ToolChoiceConfig;
using PlanFilterResult = llm::PlanFilterResult;
}  // namespace ben_gear
