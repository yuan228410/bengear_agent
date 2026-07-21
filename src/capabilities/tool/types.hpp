#pragma once

#include <vector>
#include "base/utils/json.hpp"
#include "acp/types/tool_call_types.hpp"

#include <optional>
#include <string>

namespace ben_gear::capabilities::tool {


/// 工具参数 Schema
struct ToolParameterSchema {
    std::string type = std::string("string");
    std::string description;
    std::vector<std::string> enum_values = {};  // 显式初始化，避免 GCC 警告
    bool required = true;
};

/// 工具定义（协议无关）
struct ToolDefinition {
    std::string name;
    std::string description;
    std::vector<std::pair<std::string, ToolParameterSchema>> parameters;
    bool read_only = false;  // 只读工具标记：plan 模式下只允许只读工具

    /// 转换为 OpenAI 格式
    Json to_openai_format() const;

    /// 转换为 Anthropic 格式
    Json to_anthropic_format() const;
};

// 使用 ACP 层定义的工具调用类型
using ToolCallRequest = ::ben_gear::acp::ToolCallRequest;
using ToolCallResult = ::ben_gear::acp::ToolCallResult;

/// 工具执行结果
struct ToolResult {
    bool success = true;
    std::string output;
    std::string error;

    static ToolResult ok(std::string result) {
        return {true, std::move(result), {}};
    }
    static ToolResult not_found(std::string_view name) {
        std::string msg("tool not found: ");
        msg += name;
        return {false, {}, std::move(msg)};
    }
    static ToolResult execution_error(std::string_view name,
                                       std::string_view what) {
        std::string msg("tool '");
        msg += name;
        msg += "' failed: ";
        msg += what;
        return {false, {}, std::move(msg)};
    }
    static ToolResult unknown_error(std::string_view name) {
        std::string msg("tool '");
        msg += name;
        msg += "' failed: unknown exception";
        return {false, {}, std::move(msg)};
    }
};

/// 工具选择策略
enum class ToolChoice { auto_, none, required, specific };

/// 工具选择配置
struct ToolChoiceConfig {
    ToolChoice choice = ToolChoice::auto_;
    std::optional<std::string> tool_name;

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

}  // namespace ben_gear::capabilities::tool

namespace ben_gear {
using ToolDefinition = capabilities::tool::ToolDefinition;
using ToolCallRequest = ::ben_gear::acp::ToolCallRequest;
using ToolCallResult = ::ben_gear::acp::ToolCallResult;
using ToolChoice = capabilities::tool::ToolChoice;
}  // namespace ben_gear
