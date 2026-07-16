#include "llm/provider_client.hpp"
#include "llm/provider_registry.hpp"
#include "capabilities/tool/registry.hpp"

#include "llm/anthropic_client.hpp"
#include "llm/openai_client.hpp"

namespace ben_gear::llm {

// 内置 Provider 工厂注册（static init 时自动注册）
namespace {

ProviderClient::ClientFns make_anthropic_fns(const config::Settings& settings,
                                             std::shared_ptr<net::HttpClient> http) {
    return {std::make_shared<AnthropicClient>(settings, http)};
}

ProviderClient::ClientFns make_openai_fns(const config::Settings& settings,
                                          std::shared_ptr<net::HttpClient> http) {
    return {std::make_shared<OpenAiClient>(settings, http)};
}

// 静态注册：程序启动时自动把内置 Provider 录入注册表
BEN_GEAR_REGISTER_PROVIDER(anthropic, make_anthropic_fns)
BEN_GEAR_REGISTER_PROVIDER(openai, make_openai_fns)

} // anonymous namespace

net::Task<Json> ProviderClient::chat_with_tools_async(net::EventLoop& loop,
                                        const llm::ConversationHistory& history,
                                        const capabilities::tool::ToolRegistry& tools,
                                        const capabilities::tool::ToolChoiceConfig& tool_choice,
                                        const net::CancellationToken& cancel,
                                        const std::string& model_override) {
   auto start = std::chrono::steady_clock::now();
   log_llm_request(false, true);

   auto result = co_await with_failover(cancel, [&](const ClientFns& client, const std::string&) -> net::Task<Json> {
    co_return co_await client.provider->chat_with_tools_async(loop, history, tools, tool_choice, cancel);
   }, model_override);

   auto latency = build_latency(start);
   auto usage = extract_usage_auto(result);
   usage_tracker_.record(usage, latency);
   log_llm_response(0, usage, latency);
   co_return result;
}

net::Task<StreamResult> ProviderClient::chat_stream_with_tools_async(net::EventLoop& loop,
                                                        const llm::ConversationHistory& history,
                                                        const capabilities::tool::ToolRegistry& tools,
                                                        const capabilities::tool::ToolChoiceConfig& tool_choice,
                                                        StreamHandlers handlers,
                                                        const net::CancellationToken& cancel,
                                                        const std::string& model_override) {
   auto start = std::chrono::steady_clock::now();
   log_llm_request(true, true);

   auto result = co_await with_failover(cancel, [&](const ClientFns& client, const std::string&) -> net::Task<StreamResult> {
    auto ttfb = std::make_shared<TtfbCapture>();
    StreamHandlers attempt_hs(
        TtfbCapture::wrap_shared(ttfb, handlers.on_token),
        handlers.on_thinking,
        handlers.on_tool_call,
        handlers.on_stop);
    attempt_hs.usage_out = handlers.usage_out;
    auto r = co_await client.provider->chat_stream_with_tools_async(loop, history, tools, tool_choice, std::move(attempt_hs), cancel);
    finalize_stream_result(r, start, *ttfb);
    co_return r;
   }, model_override);

   co_return result;
}

ProviderClient::ClientFns ProviderClient::make_client_fns(const config::Settings& settings) const {
   // 从注册表获取工厂，避免硬编码 switch/if-else
   auto factory = ProviderRegistry::instance().get_factory(settings.provider);
   return factory(settings, http_);
}

} // namespace ben_gear::llm