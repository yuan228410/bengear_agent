#include "llm/provider_client.hpp"
#include <chrono>
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

// ─── IProviderClient override 实现（不带 model_override）──────────

net::Task<Json> ProviderClient::chat(net::EventLoop& loop,
                       const ConversationHistory& history,
                       const capabilities::tool::ToolRegistry& tools,
                       const capabilities::tool::ToolChoiceConfig& tool_choice,
                       const net::CancellationToken& cancel) {
   auto start = std::chrono::steady_clock::now();
   log_llm_request(false);

   auto result = co_await with_failover(cancel, [&](const ClientFns& client, const std::string&) -> net::Task<Json> {
    co_return co_await client.provider->chat(loop, history, tools, tool_choice, cancel);
   });

   auto latency = build_latency(start);
   auto usage = extract_usage_auto(result);
   usage_tracker_.record(usage, latency);
   log_llm_response(200, usage, latency);
   co_return result;
}

net::Task<StreamResult> ProviderClient::chat_stream(net::EventLoop& loop,
                                      const ConversationHistory& history,
                                      const capabilities::tool::ToolRegistry& tools,
                                      const capabilities::tool::ToolChoiceConfig& tool_choice,
                                      StreamHandlers handlers,
                                      const net::CancellationToken& cancel) {
   auto start = std::chrono::steady_clock::now();
   log_llm_request(true);

   auto result = co_await with_failover(cancel, [&](const ClientFns& client, const std::string&) -> net::Task<StreamResult> {
    auto ttfb = std::make_shared<TtfbCapture>();
    StreamHandlers attempt_hs(
        TtfbCapture::wrap_shared(ttfb, handlers.on_token),
        handlers.on_thinking,
        handlers.on_tool_call,
        handlers.on_stop);
    attempt_hs.usage_out = handlers.usage_out;
    auto r = co_await client.provider->chat_stream(loop, history, tools, tool_choice, std::move(attempt_hs), cancel);
    finalize_stream_result(r, start, *ttfb);
    co_return r;
   });

   co_return result;
}

void ProviderClient::emit_non_stream_result(const Json& response, StreamHandlers& handlers) {
   // 用 primary candidate 的 provider 来 emit
   auto client = make_client_fns(settings_);
   client.provider->emit_non_stream_result(response, handlers);
}

// ─── 扩展方法：带 model_override 的重载 ──────────────────────────

net::Task<Json> ProviderClient::chat(net::EventLoop& loop,
                       const ConversationHistory& history,
                       const capabilities::tool::ToolRegistry& tools,
                       const capabilities::tool::ToolChoiceConfig& tool_choice,
                       const net::CancellationToken& cancel,
                       const std::string& model_override) {
   auto start = std::chrono::steady_clock::now();
   log_llm_request(false);

   auto result = co_await with_failover(cancel, [&](const ClientFns& client, const std::string&) -> net::Task<Json> {
    co_return co_await client.provider->chat(loop, history, tools, tool_choice, cancel);
   }, model_override);

   auto latency = build_latency(start);
   auto usage = extract_usage_auto(result);
   usage_tracker_.record(usage, latency);
   log_llm_response(200, usage, latency);
   co_return result;
}

net::Task<StreamResult> ProviderClient::chat_stream(net::EventLoop& loop,
                                      const ConversationHistory& history,
                                      const capabilities::tool::ToolRegistry& tools,
                                      const capabilities::tool::ToolChoiceConfig& tool_choice,
                                      StreamHandlers handlers,
                                      const net::CancellationToken& cancel,
                                      const std::string& model_override) {
   auto start = std::chrono::steady_clock::now();
   log_llm_request(true);

   auto result = co_await with_failover(cancel, [&](const ClientFns& client, const std::string&) -> net::Task<StreamResult> {
    auto ttfb = std::make_shared<TtfbCapture>();
    StreamHandlers attempt_hs(
        TtfbCapture::wrap_shared(ttfb, handlers.on_token),
        handlers.on_thinking,
        handlers.on_tool_call,
        handlers.on_stop);
    attempt_hs.usage_out = handlers.usage_out;
    auto r = co_await client.provider->chat_stream(loop, history, tools, tool_choice, std::move(attempt_hs), cancel);
    finalize_stream_result(r, start, *ttfb);
    co_return r;
   }, model_override);

   co_return result;
}

// ─── 非流式 + handlers 回调一步完成 ──────────────────────────────

net::Task<StreamResult> ProviderClient::chat_non_stream(net::EventLoop& loop,
                                           const ConversationHistory& history,
                                           const capabilities::tool::ToolRegistry& tools,
                                           const capabilities::tool::ToolChoiceConfig& tool_choice,
                                           StreamHandlers handlers,
                                           const net::CancellationToken& cancel,
                                           const std::string& model_override) {
   auto start = std::chrono::steady_clock::now();
   log_llm_request(false);

   // 在 with_failover lambda 内同时完成 chat + emit，确保用同一个 provider 实例
   auto result = co_await with_failover(cancel, [&](const ClientFns& client, const std::string&) -> net::Task<Json> {
    co_return co_await client.provider->chat(loop, history, tools, tool_choice, cancel);
   }, model_override);

   // 解析 JSON，通过 handlers 回调发出
   // 用 primary provider 的格式解析（当前 failover 只在同 provider 内切换，格式一致）
   auto primary_client = make_client_fns(settings_);
   primary_client.provider->emit_non_stream_result(result, handlers);

   auto latency = build_latency(start);
   auto usage = extract_usage_auto(result);
   if (handlers.usage_out) {
       usage = *handlers.usage_out;
   }
   usage_tracker_.record(usage, latency);
   log_llm_response(200, usage, latency);

   StreamResult r;
   r.status = 200;
   r.raw = result.dump();
   r.usage = usage;
   r.latency = latency;
   co_return r;
}

ProviderClient::ClientFns ProviderClient::make_client_fns(const config::Settings& settings) const {
   // 从注册表获取工厂，避免硬编码 switch/if-else
    auto factory = ProviderRegistry::instance().get_factory(settings.llm.provider);
   return factory(settings, http_);
}

net::Task<ChatResult> ProviderClient::chat_simple(net::EventLoop& loop,
                                     const std::string& system_prompt,
                                     const std::string& user_prompt,
                                     const net::CancellationToken& cancel) {
   // 构造简单对话历史
   ConversationHistory tmp;
   tmp.set_system_prompt(system_prompt);
   tmp.add_user(std::string_view(user_prompt.data(), user_prompt.size()));

   // 空 ToolRegistry — plan 解析等场景不需要工具
   capabilities::tool::ToolRegistry empty_tools;

   auto json = co_await chat(loop, tmp, empty_tools, {}, cancel);

   // 用 provider 解析 JSON 提取文本
   ChatResult result;
   result.raw = json.dump();
   result.status = 200;

   // 用 emit_non_stream_result + 临时 handlers 提取文本
   std::string text;
   TokenUsage usage;
   StreamHandlers handlers;
   handlers.on_token = [&](std::string_view token) { text += token; };
   handlers.usage_out = std::make_shared<TokenUsage>();

   auto client = make_client_fns(settings_);
   client.provider->emit_non_stream_result(json, handlers);

   result.text = std::move(text);
   result.usage = *handlers.usage_out;
   result.latency = {};

   co_return result;
}

} // namespace ben_gear::llm
