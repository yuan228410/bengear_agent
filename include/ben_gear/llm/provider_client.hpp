#pragma once

#include "ben_gear/config/settings.hpp"
#include "ben_gear/llm/anthropic_client.hpp"
#include "ben_gear/llm/chat.hpp"
#include "ben_gear/llm/cooldown_tracker.hpp"
#include "ben_gear/llm/ttfb_capture.hpp"
#include "ben_gear/llm/usage_helpers.hpp"
#include "ben_gear/llm/openai_client.hpp"
#include "ben_gear/llm/retry.hpp"
#include "ben_gear/tool/registry.hpp"
#include "ben_gear/tool/types.hpp"
#include "ben_gear/workspace/conversation_history.hpp"
#include "ben_gear/base/log/logger.hpp"
#include "ben_gear/base/net/event_loop.hpp"

#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>
#include <chrono>
#include <cstdio>

namespace ben_gear::llm {

/// Provider 协议客户端 — 内置备用模型故障转移
///
/// 请求失败时自动切换到 fallback_models 中的下一个可用模型，
/// 冷却期内的模型会被跳过，成功后清除冷却。
class ProviderClient {
public:
 explicit ProviderClient(config::Settings settings)
  : settings_(std::move(settings)),
    http_(std::make_shared<net::HttpClient>(net::to_pool_config(settings_.connection_pool))),
    cooldown_(),
    failover_enabled_(!settings_.fallback_models.empty()) {
  rebuild_client();
  log::info_fmt("provider client created: provider={}, model={}, failover={}",
                settings_.provider == config::Provider::anthropic ? "anthropic" : "openai",
                settings_.model, failover_enabled_);
 }

 /// 非流式聊天（含计时、usage 记录、全链路日志）
 net::Task<ChatResult> chat_async(net::EventLoop& loop, const ChatRequest& request,
                                 const net::CancellationToken& cancel = {}) {
  auto start = std::chrono::steady_clock::now();
  log_llm_request(false, false);

  auto result = co_await with_failover(loop, cancel, [&](const std::string&) -> net::Task<ChatResult> {
   co_return co_await chat_async_fn_(loop, request);
  });

  auto latency = build_latency(start);
  result.latency = latency;
  usage_tracker_.record(result.usage, latency);
  log_llm_response(result.status, result.usage, latency);
  co_return result;
 }

 /// 非流式带工具聊天
 net::Task<Json> chat_with_tools_async(net::EventLoop& loop,
                                       const workspace::ConversationHistory& history,
                                       const ToolRegistry& tools,
                                       const ToolChoiceConfig& tool_choice = {},
                                       const net::CancellationToken& cancel = {}) {
  auto start = std::chrono::steady_clock::now();
  log_llm_request(false, true);

  auto result = co_await with_failover(loop, cancel, [&](const std::string&) -> net::Task<Json> {
   co_return co_await chat_with_tools_async_fn_(loop, history, tools, tool_choice);
  });

  auto latency = build_latency(start);
  auto usage = extract_usage_auto(result);
  usage_tracker_.record(usage, latency);
  log_llm_response(0, usage, latency);
  co_return result;
 }

 /// 流式聊天
 net::Task<StreamResult> chat_stream_async(net::EventLoop& loop, const ChatRequest& request,
                                          StreamHandlers handlers,
                                          const net::CancellationToken& cancel = {}) {
  auto start = std::chrono::steady_clock::now();
  log_llm_request(true, false);

  TtfbCapture ttfb;
  handlers.on_token = ttfb.wrap(std::move(handlers.on_token));

  auto result = co_await with_failover(loop, cancel, [&](const std::string&) -> net::Task<StreamResult> {
   co_return co_await chat_stream_async_fn_(loop, request, handlers);
  });

  finalize_stream_result(result, start, ttfb);
  co_return result;
 }

 /// 流式带工具聊天（主活跃路径）
 net::Task<StreamResult> chat_stream_with_tools_async(net::EventLoop& loop,
                                                      const workspace::ConversationHistory& history,
                                                      const ToolRegistry& tools,
                                                      const ToolChoiceConfig& tool_choice,
                                                      StreamHandlers handlers,
                                                     const net::CancellationToken& cancel = {}) {
  auto start = std::chrono::steady_clock::now();
  log_llm_request(true, true);

  TtfbCapture ttfb;
  handlers.on_token = ttfb.wrap(std::move(handlers.on_token));

  auto result = co_await with_failover(loop, cancel, [&](const std::string&) -> net::Task<StreamResult> {
   co_return co_await chat_stream_with_tools_async_fn_(loop, history, tools, tool_choice, handlers);
  });

  finalize_stream_result(result, start, ttfb);
  co_return result;
 }

 const config::Settings& settings() const { return settings_; }
 std::shared_ptr<net::HttpClient> http() const { return http_; }
 const CooldownTracker& cooldown() const { return cooldown_; }
 UsageTracker& usage_tracker() { return usage_tracker_; }
 const UsageTracker& usage_tracker() const { return usage_tracker_; }

private:
 /// 日志：请求开始
 void log_llm_request(bool stream, bool tools) const {
  log::info_fmt("llm request: provider={}, model={}, stream={}, tools={}",
                settings_.provider == config::Provider::anthropic ? "anthropic" : "openai",
                settings_.model, stream, tools);
 }

 /// 日志：请求完成（含 usage + latency）
 void log_llm_response(int status, const TokenUsage& usage, const RequestLatency& latency) const {
  std::string extra;
  if (latency.has_ttfb) {
   char buf[32];
   std::snprintf(buf, sizeof(buf), ", ttfb=%.3fs", latency.ttfb_seconds);
   extra = buf;
  }
  log::info_fmt("llm response: status={}, prompt={}, completion={}, total={}, latency={:.2f}s{}",
                status, usage.prompt_tokens, usage.completion_tokens,
                usage.total_tokens, latency.total_seconds, extra);
 }

 /// 构建非流式延迟（无 TTFB）
 static RequestLatency build_latency(std::chrono::steady_clock::time_point start) {
  RequestLatency latency;
  latency.total_seconds = std::chrono::duration<double>(
      std::chrono::steady_clock::now() - start).count();
  return latency;
 }

 /// 流式结果收尾：补全 usage + latency，记录到 tracker，打日志
 void finalize_stream_result(StreamResult& result,
                             std::chrono::steady_clock::time_point start,
                             const TtfbCapture& ttfb) {
 auto latency = ttfb.build_latency(start);
 result.latency = latency;
 usage_tracker_.record(result.usage, latency);
 log_llm_response(result.status, result.usage, latency);
}

 /// 故障转移骨架：尝试主模型，失败后遍历 fallback chain
 template <typename F>
 net::Task<typename std::decay_t<decltype(std::declval<F>()(std::string()))>::value_type>
 with_failover(net::EventLoop& /*loop*/, const net::CancellationToken& cancel, F&& fn) {

  // 构建候选列表：主模型 ref + fallback chain 中不在冷却的
  std::vector<std::string> candidates;
  // 主模型用 config_provider_name:display_name 格式，与 fallback_models 对齐
  auto primary = settings_.config_provider_name.to_std_string() +
                  ":" + settings_.display_name.to_std_string();
  candidates.push_back(primary);

  // 保存原始 LLM 配置，failover 结束后恢复（确保下次请求仍尝试主模型）
  config::Settings original_llm;
  settings_.apply_llm_fields_to(original_llm);
  auto restore_settings = [&](bool also_rebuild) {
   original_llm.apply_llm_fields_to(settings_);
   if (also_rebuild) rebuild_client();
  };

  if (failover_enabled_) {
   for (const auto& fb : settings_.fallback_models) {
    if (cooldown_.is_in_cooldown(fb)) {
     log::info_fmt("failover: skipping [{}] (cooldown remaining={}s)", fb, cooldown_.cooldown_remaining(fb).count());
    } else {
     candidates.push_back(fb);
    }
   }
  }

  log::info_fmt("failover: candidates={} (primary={})", candidates.size(), primary);

  std::string last_error;

  for (size_t i = 0; i < candidates.size(); ++i) {
   cancel.throw_if_cancelled();
   const auto& model = candidates[i];

   // 切换模型（非主模型时从 resolved_fallbacks 查找完整配置）
   if (i > 0) {
    log::info_fmt("failover: trying fallback [{}/{}] model=[{}]", i, candidates.size() - 1, model);
    auto it = settings_.resolved_fallbacks.find(model);
    if (it != settings_.resolved_fallbacks.end()) {
     it->second.apply_llm_fields_to(settings_);
     log::info_fmt("failover: switched to provider={}, model={}, base_url={}",
                   settings_.provider == config::Provider::anthropic ? "anthropic" : "openai",
                   settings_.model, settings_.base_url);
    } else {
     log::error_fmt("failover: no resolved config for '{}', skipping", model);
     continue;
    }
    rebuild_client();
   }

   try {
    auto result = co_await fn(model);
    cooldown_.record_success(model);
    // 成功后恢复原始配置，下次请求仍从主模型开始
    // 仅在切换过模型时才 rebuild（主模型成功时跳过，避免无谓重建）
    restore_settings(i > 0);
    log::info_fmt("failover: request succeeded on model=[{}]", model);
    co_return result;
   } catch (const ProviderError& e) {
    cooldown_.record_failure(model, e.kind(), e.retry_after_seconds());
    last_error = e.what();
    log::error_fmt("failover: model=[{}] failed with provider error: kind={}, msg={}",
                   model, static_cast<int>(e.kind()), last_error);
    if (!is_retryable_error(e.kind()) || i == candidates.size() - 1) {
     restore_settings(false);
     // 非重试错误或最后一个候选：抛出
     throw;
    }
   } catch (const std::exception& e) {
    cooldown_.record_failure(model, ProviderErrorKind::transient);
    last_error = e.what();
    log::error_fmt("failover: model=[{}] failed with exception: {}", model, last_error);
    if (i == candidates.size() - 1) {
     restore_settings(false);
     throw;
    }
   }
  }

  // 所有候选都失败
  throw std::runtime_error("all models failed: " + last_error);
 }

 /// 根据 settings_ 重建底层客户端
 void rebuild_client() {
  if (settings_.api_key.empty()) return; // 允许测试场景空 key 构造
  if (settings_.provider == config::Provider::anthropic) {
   auto client = std::make_unique<AnthropicClient>(settings_, http_);
   bind_all<AnthropicClient>(client.get());
   client_storage_ = std::make_unique<ClientStorage<AnthropicClient>>(std::move(client));
  } else {
   auto client = std::make_unique<OpenAiClient>(settings_, http_);
   bind_all<OpenAiClient>(client.get());
   client_storage_ = std::make_unique<ClientStorage<OpenAiClient>>(std::move(client));
  }
 }

 void ensure_api_key() const {
  if (settings_.api_key.empty()) {
   throw std::runtime_error("missing api key");
  }
 }

 struct IClientStorage { virtual ~IClientStorage() = default; };
 template <typename T>
 struct ClientStorage : IClientStorage {
  std::unique_ptr<T> client;
  explicit ClientStorage(std::unique_ptr<T> c) : client(std::move(c)) {}
 };
 std::unique_ptr<IClientStorage> client_storage_;

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
 CooldownTracker cooldown_;
 UsageTracker usage_tracker_;
 bool failover_enabled_;

 std::function<net::Task<ChatResult>(net::EventLoop&, const ChatRequest&)> chat_async_fn_;
 std::function<net::Task<Json>(net::EventLoop&, const workspace::ConversationHistory&, const ToolRegistry&, const ToolChoiceConfig&)> chat_with_tools_async_fn_;
 std::function<net::Task<StreamResult>(net::EventLoop&, const ChatRequest&, StreamHandlers)> chat_stream_async_fn_;
 std::function<net::Task<StreamResult>(net::EventLoop&, const workspace::ConversationHistory&, const ToolRegistry&, const ToolChoiceConfig&, StreamHandlers)> chat_stream_with_tools_async_fn_;
};

} // namespace ben_gear::llm

namespace ben_gear {
using ProviderClient = llm::ProviderClient;
} // namespace ben_gear
