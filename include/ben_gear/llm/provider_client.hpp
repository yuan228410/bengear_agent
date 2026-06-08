#pragma once

#include "ben_gear/config/settings.hpp"
#include "ben_gear/llm/anthropic_client.hpp"
#include "ben_gear/llm/chat.hpp"
#include "ben_gear/workspace/conversation_history.hpp"
#include "ben_gear/llm/openai_client.hpp"
#include "ben_gear/tool/registry.hpp"
#include "ben_gear/tool/types.hpp"
#include "ben_gear/base/log/logger.hpp"

#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>

namespace ben_gear::llm {

/// Provider 协议客户端统一接口
/// 构造时一次性绑定具体客户端，消除运行时 variant 分发开销
class ProviderClient {
public:
    explicit ProviderClient(config::Settings settings)
        : settings_(std::move(settings)),
          http_(std::make_shared<net::HttpClient>(net::to_pool_config(settings_.connection_pool))) {
        // 构造时一次性创建具体客户端和函数绑定，后续零分发开销
        if (settings_.provider == config::Provider::anthropic) {
            auto client = std::make_unique<AnthropicClient>(settings_, http_);
            bind_all<AnthropicClient>(client.get());
            client_storage_ = std::make_unique<ClientStorage<AnthropicClient>>(std::move(client));
        } else {
            auto client = std::make_unique<OpenAiClient>(settings_, http_);
            bind_all<OpenAiClient>(client.get());
            client_storage_ = std::make_unique<ClientStorage<OpenAiClient>>(std::move(client));
        }
        ben_gear::log::info_fmt("provider client created: provider={}, model={}, base_url={}",
            settings_.provider == config::Provider::anthropic ? "anthropic" : "openai",
            settings_.model, settings_.base_url);
    }

    net::Task<ChatResult> chat_async(net::EventLoop& loop, const ChatRequest& request) const {
        ensure_api_key();
        co_return co_await chat_async_fn_(loop, request);
    }

    /// 带工具的异步聊天
    net::Task<Json> chat_with_tools_async(net::EventLoop& loop,
                                          const workspace::ConversationHistory& history,
                                          const ToolRegistry& tools,
                                          const ToolChoiceConfig& tool_choice = {}) const {
        ensure_api_key();
        co_return co_await chat_with_tools_async_fn_(loop, history, tools, tool_choice);
    }

    net::Task<StreamResult> chat_stream_async(net::EventLoop& loop, const ChatRequest& request, StreamHandlers handlers) const {
        ensure_api_key();
        co_return co_await chat_stream_async_fn_(loop, request, std::move(handlers));
    }

    /// 带工具的异步流式聊天
    net::Task<StreamResult> chat_stream_with_tools_async(net::EventLoop& loop,
                                                         const workspace::ConversationHistory& history,
                                                         const ToolRegistry& tools,
                                                         const ToolChoiceConfig& tool_choice,
                                                         StreamHandlers handlers) const {
        ensure_api_key();
        co_return co_await chat_stream_with_tools_async_fn_(loop, history, tools, tool_choice, std::move(handlers));
    }

private:
    void ensure_api_key() const {
        if (settings_.api_key.empty()) {
            ben_gear::log::error_fmt("API key is empty, provider={}, model={}",
                settings_.provider == config::Provider::anthropic ? "anthropic" : "openai", settings_.model);
            throw std::runtime_error(
                "Missing API key. Please set it via:\n"
                "  1. Environment variable: export BEN_GEAR_API_KEY=your_key\n"
                "  2. Config file: {\"api_key\": \"your_key\"}\n"
                "  3. CLI argument: --api-key your_key"
            );
        }
    }

    // 类型擦除存储：持有具体客户端的所有权，确保函数绑定中的裸指针有效
    struct IClientStorage {
        virtual ~IClientStorage() = default;
    };

    template <typename T>
    struct ClientStorage : IClientStorage {
        std::unique_ptr<T> client;
        explicit ClientStorage(std::unique_ptr<T> c) : client(std::move(c)) {}
    };

    std::unique_ptr<IClientStorage> client_storage_;

    // 构造时一次性绑定所有方法，消除运行时 variant/if-else 分发
    template <typename T>
    void bind_all(T* client) {
        chat_async_fn_ = [client](net::EventLoop& loop, const ChatRequest& req) -> net::Task<ChatResult> {
            co_return co_await client->chat_async(loop, req);
        };
        chat_with_tools_async_fn_ = [client](net::EventLoop& loop, const workspace::ConversationHistory& h,
                                              const ToolRegistry& t, const ToolChoiceConfig& tc) -> net::Task<Json> {
            co_return co_await client->chat_with_tools_async(loop, h, t, tc);
        };
        chat_stream_async_fn_ = [client](net::EventLoop& loop, const ChatRequest& req,
                                          StreamHandlers h) -> net::Task<StreamResult> {
            co_return co_await client->chat_stream_async(loop, req, std::move(h));
        };
        chat_stream_with_tools_async_fn_ = [client](net::EventLoop& loop, const workspace::ConversationHistory& h,
                                                      const ToolRegistry& t, const ToolChoiceConfig& tc,
                                                      StreamHandlers hs) -> net::Task<StreamResult> {
            co_return co_await client->chat_stream_with_tools_async(loop, h, t, tc, std::move(hs));
        };
    }

    config::Settings settings_;
    std::shared_ptr<net::HttpClient> http_;

    // 异步函数绑定：构造时确定，调用时零分发开销
    std::function<net::Task<ChatResult>(net::EventLoop&, const ChatRequest&)> chat_async_fn_;
    std::function<net::Task<Json>(net::EventLoop&, const workspace::ConversationHistory&, const ToolRegistry&, const ToolChoiceConfig&)> chat_with_tools_async_fn_;
    std::function<net::Task<StreamResult>(net::EventLoop&, const ChatRequest&, StreamHandlers)> chat_stream_async_fn_;
    std::function<net::Task<StreamResult>(net::EventLoop&, const workspace::ConversationHistory&, const ToolRegistry&, const ToolChoiceConfig&, StreamHandlers)> chat_stream_with_tools_async_fn_;
};

}  // namespace ben_gear::llm

namespace ben_gear {
using ProviderClient = llm::ProviderClient;
}  // namespace ben_gear
