#pragma once

#include "net/event_loop.hpp"
#include "base/utils/json.hpp"
#include "llm/conversation_history.hpp"
#include "llm/stream.hpp"
#include "capabilities/tool/types.hpp"

// ToolRegistry 只需前向声明（不在本头文件中使用其成员）
namespace ben_gear::capabilities::tool {
class ToolRegistry;
} // namespace ben_gear::capabilities::tool

namespace ben_gear::llm {

/// LLM Provider 客户端虚基类
///
/// 只提供两个核心方法：
/// - chat_stream: 流式请求（带工具），通过 StreamHandlers 回调发出增量内容
/// - chat: 非流式请求（带工具），返回完整 JSON
///
/// emit_non_stream_result: 将非流式 JSON 解析后通过 StreamHandlers 回调发出，
/// 用于 stream=false 时在统一入口分流调用。
///
/// 方法 intentionally 非 const：LLM 调用是有状态的
///（usage 追踪、故障转移冷却、连接池管理等）。
class IProviderClient {
public:
    virtual ~IProviderClient() = default;

    /// 流式聊天（带工具），结果通过 StreamHandlers 回调发出
    virtual net::Task<StreamResult> chat_stream(net::EventLoop& loop,
                                                 const ConversationHistory& history,
                                                 const capabilities::tool::ToolRegistry& tools,
                                                 const capabilities::tool::ToolChoiceConfig& tool_choice,
                                                 StreamHandlers handlers,
                                                 const net::CancellationToken& cancel = {}) = 0;

    /// 非流式聊天（带工具），返回完整 JSON 响应
    virtual net::Task<Json> chat(net::EventLoop& loop,
                                  const ConversationHistory& history,
                                  const capabilities::tool::ToolRegistry& tools,
                                  const capabilities::tool::ToolChoiceConfig& tool_choice = {},
                                  const net::CancellationToken& cancel = {}) = 0;

    /// 将非流式响应 JSON 解析后通过 StreamHandlers 回调发出
    /// 每个 provider 实现自己的格式解析（OpenAI/Anthropic 响应结构不同）
    virtual void emit_non_stream_result(const Json& response, StreamHandlers& handlers) = 0;
};

}  // namespace ben_gear::llm
