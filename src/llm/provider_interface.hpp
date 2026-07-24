#pragma once

#include "net/event_loop.hpp"
#include "base/utils/json.hpp"
#include "llm/chat.hpp"
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
/// OpenAiClient / AnthropicClient / ProviderClient 均实现此接口。
/// 方法 intentionally 非 const：LLM 调用是有状态的
///（usage 追踪、故障转移冷却、连接池管理等）。
class IProviderClient {
public:
    virtual ~IProviderClient() = default;

    virtual net::Task<ChatResult> chat_async(net::EventLoop& loop, const ChatRequest& request,
                                             const net::CancellationToken& cancel = {}) = 0;

    virtual net::Task<Json> chat_with_tools_async(net::EventLoop& loop,
                                                   const ConversationHistory& history,
                                                   const capabilities::tool::ToolRegistry& tools,
                                                   const capabilities::tool::ToolChoiceConfig& tool_choice = {},
                                                   const net::CancellationToken& cancel = {}) = 0;

    virtual net::Task<StreamResult> chat_stream_async(net::EventLoop& loop, const ChatRequest& request,
                                                       StreamHandlers handlers,
                                                       const net::CancellationToken& cancel = {}) = 0;

    virtual net::Task<StreamResult> chat_stream_with_tools_async(net::EventLoop& loop,
                                                                  const ConversationHistory& history,
                                                                  const capabilities::tool::ToolRegistry& tools,
                                                                  const capabilities::tool::ToolChoiceConfig& tool_choice,
                                                                  StreamHandlers handlers,
                                                                  const net::CancellationToken& cancel = {}) = 0;
};

}  // namespace ben_gear::llm
