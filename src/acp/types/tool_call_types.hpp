#pragma once

#include "base/utils/json.hpp"
#include <string>

namespace ben_gear::acp {

/// 工具调用请求（ACP 协议层定义）
/// 
/// 这是 ACP 协议层定义的工具调用请求，不依赖具体工具实现。
/// capabilities 模块和 LLM 模块都使用这些类型。
struct ToolCallRequest {
    std::string id;
    std::string name;
    Json arguments = Json::object();

    /// 从 OpenAI 格式解析
    static ToolCallRequest from_openai(const Json& j);

    /// 从 Anthropic 格式解析
    static ToolCallRequest from_anthropic(const Json& j);

    /// 转换为 OpenAI 格式工具结果消息
    Json to_openai_tool_message(const std::string& result) const;

    /// 转换为 Anthropic 格式工具结果消息
    Json to_anthropic_tool_message(const std::string& result) const;

private:
    /// 清理 LLM 内部特殊 token 泄漏
    static void sanitize_model_tokens(std::string& json_str);
};

/// 工具调用结果（ACP 协议层定义）
struct ToolCallResult {
    std::string tool_call_id;
    std::string name;
    std::string output;
    bool success = true;
};

} // namespace ben_gear::acp

namespace ben_gear {
using ToolCallRequest = acp::ToolCallRequest;
using ToolCallResult = acp::ToolCallResult;
} // namespace ben_gear
