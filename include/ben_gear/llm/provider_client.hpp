#pragma once

#include "ben_gear/config/settings.hpp"
#include "ben_gear/llm/anthropic_client.hpp"
#include "ben_gear/llm/chat.hpp"
#include "ben_gear/llm/message.hpp"
#include "ben_gear/llm/openai_client.hpp"
#include "ben_gear/tool/registry.hpp"
#include "ben_gear/tool/types.hpp"

#include <stdexcept>
#include <utility>
#include <variant>

namespace ben_gear::llm {

class ProviderClient {
public:
    explicit ProviderClient(config::Settings settings)
        : settings_(std::move(settings)),
          client_(settings_.provider == config::Provider::anthropic
                  ? ClientVariant(AnthropicClient(settings_))
                  : ClientVariant(OpenAiClient(settings_))) {}

    ChatResult chat(const ChatRequest& request) const {
        ensure_api_key();
        return std::visit([&](const auto& c) { return c.chat(request); }, client_);
    }

    net::Task<ChatResult> chat_async(net::EventLoop& loop, const ChatRequest& request) const {
        ensure_api_key();
        if (settings_.provider == config::Provider::anthropic) {
            co_return co_await std::get<AnthropicClient>(client_).chat_async(loop, request);
        }
        co_return co_await std::get<OpenAiClient>(client_).chat_async(loop, request);
    }

    /// 带工具的聊天
    Json chat_with_tools(const ConversationHistory& history,
                         const ToolRegistry& tools,
                         const ToolChoiceConfig& tool_choice = {}) const {
        ensure_api_key();
        return std::visit([&](const auto& c) { return c.chat_with_tools(history, tools, tool_choice); }, client_);
    }

    net::Task<Json> chat_with_tools_async(net::EventLoop& loop,
                                          const ConversationHistory& history,
                                          const ToolRegistry& tools,
                                          const ToolChoiceConfig& tool_choice = {}) const {
        ensure_api_key();
        if (settings_.provider == config::Provider::anthropic) {
            co_return co_await std::get<AnthropicClient>(client_).chat_with_tools_async(loop, history, tools, tool_choice);
        }
        co_return co_await std::get<OpenAiClient>(client_).chat_with_tools_async(loop, history, tools, tool_choice);
    }

    StreamResult chat_stream(const ChatRequest& request, const StreamTokenHandler& on_token) const {
        return chat_stream(request, StreamHandlers(on_token));
    }

    StreamResult chat_stream(const ChatRequest& request, StreamHandlers handlers) const {
        ensure_api_key();
        return std::visit([&](const auto& c) { return c.chat_stream(request, std::move(handlers)); }, client_);
    }

    net::Task<StreamResult> chat_stream_async(net::EventLoop& loop, const ChatRequest& request, StreamHandlers handlers) const {
        ensure_api_key();
        if (settings_.provider == config::Provider::anthropic) {
            co_return co_await std::get<AnthropicClient>(client_).chat_stream_async(loop, request, std::move(handlers));
        }
        co_return co_await std::get<OpenAiClient>(client_).chat_stream_async(loop, request, std::move(handlers));
    }

    /// 带工具的流式聊天
    StreamResult chat_stream_with_tools(const ConversationHistory& history,
                                         const ToolRegistry& tools,
                                         const ToolChoiceConfig& tool_choice,
                                         StreamHandlers handlers) const {
        ensure_api_key();
        return std::visit([&](const auto& c) { return c.chat_stream_with_tools(history, tools, tool_choice, std::move(handlers)); }, client_);
    }

    net::Task<StreamResult> chat_stream_with_tools_async(net::EventLoop& loop,
                                                          const ConversationHistory& history,
                                                          const ToolRegistry& tools,
                                                          const ToolChoiceConfig& tool_choice,
                                                          StreamHandlers handlers) const {
        ensure_api_key();
        if (settings_.provider == config::Provider::anthropic) {
            co_return co_await std::get<AnthropicClient>(client_).chat_stream_with_tools_async(loop, history, tools, tool_choice, std::move(handlers));
        }
        co_return co_await std::get<OpenAiClient>(client_).chat_stream_with_tools_async(loop, history, tools, tool_choice, std::move(handlers));
    }

private:
    void ensure_api_key() const {
        if (settings_.api_key.empty()) {
            throw std::runtime_error(
                "Missing API key. Please set it via:\n"
                "  1. Environment variable: export BEN_GEAR_API_KEY=your_key\n"
                "  2. Config file: {\"api_key\": \"your_key\"}\n"
                "  3. CLI argument: --api-key your_key"
            );
        }
    }

    using ClientVariant = std::variant<OpenAiClient, AnthropicClient>;
    config::Settings settings_;
    ClientVariant client_;
};

}  // namespace ben_gear::llm

namespace ben_gear {
using ProviderClient = llm::ProviderClient;
}  // namespace ben_gear
