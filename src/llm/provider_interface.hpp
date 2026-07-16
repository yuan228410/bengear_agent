#pragma once

#include "base/net/event_loop.hpp"
#include "base/utils/json.hpp"
#include "llm/chat.hpp"
#include "llm/conversation_history.hpp"
#include "llm/stream.hpp"
#include "capabilities/tool/registry.hpp"
#include "capabilities/tool/types.hpp"

namespace ben_gear::llm {

/// Common virtual base for LLM provider clients.
/// OpenAiClient and AnthropicClient implement this interface.
class IProviderClient {
public:
    virtual ~IProviderClient() = default;

    virtual net::Task<ChatResult> chat_async(net::EventLoop& loop, const ChatRequest& request,
                                             const net::CancellationToken& cancel = {}) const = 0;

    virtual net::Task<Json> chat_with_tools_async(net::EventLoop& loop,
                                                   const ConversationHistory& history,
                                                   const capabilities::tool::ToolRegistry& tools,
                                                   const capabilities::tool::ToolChoiceConfig& tool_choice = {},
                                                   const net::CancellationToken& cancel = {}) const = 0;

    virtual net::Task<StreamResult> chat_stream_async(net::EventLoop& loop, const ChatRequest& request,
                                                       StreamHandlers handlers,
                                                       const net::CancellationToken& cancel = {}) const = 0;

    virtual net::Task<StreamResult> chat_stream_with_tools_async(net::EventLoop& loop,
                                                                  const ConversationHistory& history,
                                                                  const capabilities::tool::ToolRegistry& tools,
                                                                  const capabilities::tool::ToolChoiceConfig& tool_choice,
                                                                  StreamHandlers handlers,
                                                                  const net::CancellationToken& cancel = {}) const = 0;
};

}  // namespace ben_gear::llm
