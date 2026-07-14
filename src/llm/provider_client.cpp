#include "llm/provider_client.hpp"
#include "llm/provider_registry.hpp"

// 工具/工作区完整类型仅在本实现文件中使用，避免在公共接口头中暴露
// 上层依赖（分层解耦）。tool/types.hpp 中的 container 别名须在
// tool/registry.hpp 之前可见，provider_client.hpp 已保证该顺序。
#include "workspace/conversation_history.hpp"

#include "llm/anthropic_client.hpp"
#include "llm/openai_client.hpp"

namespace ben_gear::llm {

// 内置 Provider 工厂注册（static init 时自动注册）
namespace {

ProviderClient::ClientFns make_anthropic_fns(const config::Settings& settings,
                                             std::shared_ptr<net::HttpClient> http) {
    ProviderClient::ClientFns fns;
    auto client = std::make_shared<AnthropicClient>(settings, http);
    fns.chat_async = [client](net::EventLoop& loop, const ChatRequest& req,
                              const net::CancellationToken& cancel) -> net::Task<ChatResult> {
        co_return co_await client->chat_async(loop, req, cancel);
    };
    fns.chat_with_tools_async = [client](net::EventLoop& loop,
                                         const workspace::ConversationHistory& h,
                                         const ToolRegistry& t, const ToolChoiceConfig& tc,
                                         const net::CancellationToken& cancel) -> net::Task<Json> {
        co_return co_await client->chat_with_tools_async(loop, h, t, tc, cancel);
    };
    fns.chat_stream_async = [client](net::EventLoop& loop, const ChatRequest& req,
                                     StreamHandlers h,
                                     const net::CancellationToken& cancel) -> net::Task<StreamResult> {
        co_return co_await client->chat_stream_async(loop, req, std::move(h), cancel);
    };
    fns.chat_stream_with_tools_async = [client](net::EventLoop& loop,
                                                const workspace::ConversationHistory& h,
                                                const ToolRegistry& t, const ToolChoiceConfig& tc,
                                                StreamHandlers hs,
                                                const net::CancellationToken& cancel) -> net::Task<StreamResult> {
        co_return co_await client->chat_stream_with_tools_async(loop, h, t, tc, std::move(hs), cancel);
    };
    return fns;
}

ProviderClient::ClientFns make_openai_fns(const config::Settings& settings,
                                          std::shared_ptr<net::HttpClient> http) {
    ProviderClient::ClientFns fns;
    auto client = std::make_shared<OpenAiClient>(settings, http);
    fns.chat_async = [client](net::EventLoop& loop, const ChatRequest& req,
                              const net::CancellationToken& cancel) -> net::Task<ChatResult> {
        co_return co_await client->chat_async(loop, req, cancel);
    };
    fns.chat_with_tools_async = [client](net::EventLoop& loop,
                                         const workspace::ConversationHistory& h,
                                         const ToolRegistry& t, const ToolChoiceConfig& tc,
                                         const net::CancellationToken& cancel) -> net::Task<Json> {
        co_return co_await client->chat_with_tools_async(loop, h, t, tc, cancel);
    };
    fns.chat_stream_async = [client](net::EventLoop& loop, const ChatRequest& req,
                                     StreamHandlers h,
                                     const net::CancellationToken& cancel) -> net::Task<StreamResult> {
        co_return co_await client->chat_stream_async(loop, req, std::move(h), cancel);
    };
    fns.chat_stream_with_tools_async = [client](net::EventLoop& loop,
                                                const workspace::ConversationHistory& h,
                                                const ToolRegistry& t, const ToolChoiceConfig& tc,
                                                StreamHandlers hs,
                                                const net::CancellationToken& cancel) -> net::Task<StreamResult> {
        co_return co_await client->chat_stream_with_tools_async(loop, h, t, tc, std::move(hs), cancel);
    };
    return fns;
}

// 静态注册：程序启动时自动把内置 Provider 录入注册表
BEN_GEAR_REGISTER_PROVIDER(anthropic, make_anthropic_fns);
BEN_GEAR_REGISTER_PROVIDER(openai, make_openai_fns);

} // anonymous namespace

net::Task<Json> ProviderClient::chat_with_tools_async(net::EventLoop& loop,
                                        const workspace::ConversationHistory& history,
                                        const ToolRegistry& tools,
                                        const ToolChoiceConfig& tool_choice,
                                        const net::CancellationToken& cancel,
                                        const base::container::String& model_override) {
   auto start = std::chrono::steady_clock::now();
   log_llm_request(false, true);

   auto result = co_await with_failover(cancel, [&](const ClientFns& client, const std::string&) -> net::Task<Json> {
    co_return co_await client.chat_with_tools_async(loop, history, tools, tool_choice, cancel);
   }, model_override);

   auto latency = build_latency(start);
   auto usage = extract_usage_auto(result);
   usage_tracker_.record(usage, latency);
   log_llm_response(0, usage, latency);
   co_return result;
}

net::Task<StreamResult> ProviderClient::chat_stream_with_tools_async(net::EventLoop& loop,
                                                        const workspace::ConversationHistory& history,
                                                        const ToolRegistry& tools,
                                                        const ToolChoiceConfig& tool_choice,
                                                        StreamHandlers handlers,
                                                        const net::CancellationToken& cancel,
                                                        const base::container::String& model_override) {
   auto start = std::chrono::steady_clock::now();
   log_llm_request(true, true);

   auto shared_hs = std::make_shared<StreamHandlers>(std::move(handlers));
   auto ttfb_ptr = std::make_shared<TtfbCapture>();

   auto result = co_await with_failover(cancel, [&, shared_hs, ttfb_ptr](const ClientFns& client, const std::string&) -> net::Task<StreamResult> {
    StreamHandlers attempt_hs(
        ttfb_ptr->wrap(shared_hs->on_token),
        shared_hs->on_thinking,
        shared_hs->on_tool_call,
        shared_hs->on_stop);
    attempt_hs.usage_out = shared_hs->usage_out;
    co_return co_await client.chat_stream_with_tools_async(loop, history, tools, tool_choice, std::move(attempt_hs), cancel);
   }, model_override);

   finalize_stream_result(result, start, *ttfb_ptr);
   co_return result;
}

ProviderClient::ClientFns ProviderClient::make_client_fns(const config::Settings& settings) const {
   // 从注册表获取工厂，避免硬编码 switch/if-else
   auto factory = ProviderRegistry::instance().get_factory(settings.provider);
   return factory(settings, http_);
}

} // namespace ben_gear::llm