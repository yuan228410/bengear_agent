#pragma once

#include "base/config/settings.hpp"
#include "llm/chat.hpp"
#include "llm/conversation_history.hpp"
#include "llm/provider_error.hpp"
#include "llm/cooldown_tracker.hpp"
#include "llm/ttfb_capture.hpp"
#include "llm/usage.hpp"
#include "llm/stream.hpp"
#include "base/log/logger.hpp"
#include "base/net/event_loop.hpp"
#include "base/net/http.hpp"
#include "llm/provider_interface.hpp"

#include <chrono>
#include <cstdio>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>


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
  log::info_fmt("provider client created: provider={}, model={}, failover={}",
                settings_.provider == config::Provider::anthropic ? "anthropic" : "openai",
                settings_.model, failover_enabled_);
 }

 /// 非流式聊天（含计时、usage 记录、全链路日志）
 net::Task<ChatResult> chat_async(net::EventLoop& loop, const ChatRequest& request,
                                 const net::CancellationToken& cancel = {}) {
  auto start = std::chrono::steady_clock::now();
  log_llm_request(false, false);

  auto result = co_await with_failover(cancel, [&](const ClientFns& client, const std::string&) -> net::Task<ChatResult> {
   co_return co_await client.provider->chat_async(loop, request, cancel);
  });

  auto latency = build_latency(start);
  result.is_context_overflow = detect_context_overflow(result.status, std::string_view(result.raw));
  result.latency = latency;
  usage_tracker_.record(result.usage, latency);
  log_llm_response(result.status, result.usage, latency);
  co_return result;
 }

  /// 非流式带工具聊天
  net::Task<Json> chat_with_tools_async(net::EventLoop& loop,
                                        const llm::ConversationHistory& history,
                                        const capabilities::tool::ToolRegistry& tools,
                                        const capabilities::tool::ToolChoiceConfig& tool_choice = {},
                                        const net::CancellationToken& cancel = {},
                                        const std::string& model_override = {});

  /// 流式聊天（含故障转移）
  net::Task<StreamResult> chat_stream_async(net::EventLoop& loop, const ChatRequest& request,
                                           StreamHandlers handlers,
                                           const net::CancellationToken& cancel = {}) {
   auto start = std::chrono::steady_clock::now();
   log_llm_request(true, false);

   auto result = co_await with_failover(cancel, [&](const ClientFns& client, const std::string&) -> net::Task<StreamResult> {
    auto ttfb = std::make_shared<TtfbCapture>();
    StreamHandlers attempt_hs(
        TtfbCapture::wrap_shared(ttfb, handlers.on_token),
        handlers.on_thinking,
        handlers.on_tool_call,
        handlers.on_stop);
    attempt_hs.usage_out = handlers.usage_out;
    auto r = co_await client.provider->chat_stream_async(loop, request, std::move(attempt_hs), cancel);
    finalize_stream_result(r, start, *ttfb);
    co_return r;
   });

   co_return result;
  }

  /// 流式带工具聊天（主活跃路径）
  net::Task<StreamResult> chat_stream_with_tools_async(net::EventLoop& loop,
                                                       const llm::ConversationHistory& history,
                                                       const capabilities::tool::ToolRegistry& tools,
                                                       const capabilities::tool::ToolChoiceConfig& tool_choice,
                                                       StreamHandlers handlers,
                                                       const net::CancellationToken& cancel = {},
                                                       const std::string& model_override = {});

 const config::Settings& settings() const { return settings_; }
 std::shared_ptr<net::HttpClient> http() const { return http_; }
 const CooldownTracker& cooldown() const { return cooldown_; }
 UsageTracker& usage_tracker() { return usage_tracker_; }
 const UsageTracker& usage_tracker() const { return usage_tracker_; }

  struct ClientFns {
   std::shared_ptr<IProviderClient> provider;
  };

 struct ProviderCandidate {
  std::string key;
  config::Settings settings;
  bool is_primary = false;
  };

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
  result.is_context_overflow = detect_context_overflow(result.status, std::string_view(result.raw));
  result.latency = latency;
  usage_tracker_.record(result.usage, latency);
  log_llm_response(result.status, result.usage, latency);
 }

  ClientFns make_client_fns(const config::Settings& settings) const;

 std::vector<ProviderCandidate> build_candidates(const std::string& model_override) {
  config::Settings base;
  bool failover_enabled = false;
  {
   std::lock_guard lock(model_switch_mutex_);
   base = settings_;
   failover_enabled = failover_enabled_;
  }

  const auto primary = base.config_provider_name + ":" + base.display_name;
  const auto override_key = model_override;
  std::vector<std::string> keys;
  keys.push_back(!model_override.empty() ? override_key : primary);
  if (!model_override.empty() && override_key != primary) {
   keys.push_back(primary);
  }
  if (failover_enabled) {
   for (const auto& fb : base.fallback_models) {
    if (cooldown_.is_in_cooldown(fb)) {
     log::info_fmt("failover: skipping [{}] (cooldown remaining={}s)", fb, cooldown_.cooldown_remaining(fb).count());
    } else {
     keys.push_back(fb);
    }
   }
  }

  std::vector<ProviderCandidate> candidates;
  candidates.reserve(keys.size());
  for (const auto& key : keys) {
   ProviderCandidate candidate;
   candidate.key = key;
   candidate.settings = base;
   candidate.is_primary = key == primary;
   const bool using_override = !model_override.empty() && key == override_key;
   if (!candidate.is_primary) {
     auto it = base.resolved_fallbacks.find(std::string(key));
    if (it != base.resolved_fallbacks.end()) {
     it->second.apply_llm_fields_to(candidate.settings);
    } else if (using_override) {
     candidate.settings.model = std::string(key);
     candidate.settings.display_name = std::string(key);
    } else {
     log::error_fmt("failover: no resolved config for '{}', skipping", key);
     continue;
    }
   }
   candidates.push_back(std::move(candidate));
  }
  log::info_fmt("failover: candidates={} (primary={})", candidates.size(), primary);
  return candidates;
 }

 /// 故障转移骨架：尝试主模型，失败后遍历 fallback chain。
 /// 只在构建候选快照时短暂加锁；候选请求使用独立 client handle，避免跨 co_await 持锁。
 template <typename F>
 net::Task<typename std::decay_t<decltype(std::declval<F>()(std::declval<const ClientFns&>(), std::string()))>::value_type>
 with_failover(const net::CancellationToken& cancel, F fn,
               const std::string& model_override = {}) {
  auto candidates = build_candidates(model_override);
  std::string last_error;

  for (size_t i = 0; i < candidates.size(); ++i) {
   cancel.throw_if_cancelled();
   const auto& candidate = candidates[i];
   if (!candidate.is_primary) {
    log::info_fmt("failover: trying candidate [{}/{}] model=[{}]", i, candidates.size() - 1, candidate.key);
    log::info_fmt("failover: using provider={}, model={}, base_url={}",
                  candidate.settings.provider == config::Provider::anthropic ? "anthropic" : "openai",
                  candidate.settings.model, candidate.settings.base_url);
   }
   auto client = make_client_fns(candidate.settings);

   try {
    auto result = co_await fn(client, candidate.key);
    cooldown_.record_success(candidate.key);
    log::info_fmt("failover: request succeeded on model=[{}]", candidate.key);
    co_return result;
   } catch (const net::OperationCancelled&) {
    throw;
   } catch (const ProviderError& e) {
    cooldown_.record_failure(candidate.key, e.kind(), e.retry_after_seconds());
    last_error = e.what();
    log::error_fmt("failover: model=[{}] failed with provider error: kind={}, msg={}",
                   candidate.key, static_cast<int>(e.kind()), last_error);
    if (!is_retryable_error(e.kind()) || i == candidates.size() - 1) {
     throw;
    }
   } catch (const std::exception& e) {
    cooldown_.record_failure(candidate.key, ProviderErrorKind::transient);
    last_error = e.what();
    log::error_fmt("failover: model=[{}] failed with exception: {}", candidate.key, last_error);
    if (i == candidates.size() - 1) {
     throw;
    }
   }
  }

  throw ProviderError(ProviderErrorKind::transient, 0,
                        "all models failed: " + last_error);
 }

 config::Settings settings_;
 std::shared_ptr<net::HttpClient> http_;
 CooldownTracker cooldown_;
 UsageTracker usage_tracker_;
 bool failover_enabled_;
 std::mutex model_switch_mutex_;
};

} // namespace ben_gear::llm

namespace ben_gear {
using ProviderClient = llm::ProviderClient;
} // namespace ben_gear
