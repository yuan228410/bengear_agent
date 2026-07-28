#include "agent/execution/loop.hpp"

#include "acp/core/message.hpp"
#include "log/logger.hpp"
#include "agent/core/events.hpp"
#include "agent/core/span_events.hpp"
#include "capabilities/tool/manager.hpp"
#include "llm/adapter.hpp"
#include "llm/provider_error.hpp"
#include "llm/stream.hpp"
#include <chrono>

namespace ben_gear::agent::execution {

using ToolCallManager = capabilities::tool::ToolCallManager;

// ─── 助手函数 ────────────────────────────────────────────────────────

namespace {

std::vector<acp::ToolCallRequest> extract_tool_calls(
    const acp::ACPMessage& msg) {
    std::vector<acp::ToolCallRequest> calls;
    for (const auto& block : msg.content()) {
        if (block.is_tool_use())
            calls.push_back(block.tool_use());
    }
    return calls;
}

std::string extract_text(const acp::ACPMessage& msg) {
    std::string text;
    for (const auto& block : msg.content()) {
        if (block.is_text() && !block.is_thinking())
            text.append(block.text());
    }
    return text;
}

std::string extract_thinking(const acp::ACPMessage& msg) {
    for (const auto& block : msg.content()) {
        if (block.is_thinking())
            return block.text();
    }
    return {};
}

/// 统计非 update_todo 的工具调用数
int count_budgeted(const std::vector<acp::ToolCallRequest>& calls) {
    int n = 0;
    for (const auto& c : calls) {
        if (std::string_view(c.name.data(), c.name.size()) != "update_todo")
            ++n;
    }
    return n;
}

} // namespace

// ─── ExecutionLoop ────────────────────────────────────────────────────

ExecutionLoop::ExecutionLoop(LoopConfig config,
                             IExecutionLoopServices& services,
                             std::shared_ptr<base::concurrency::ThreadPool> pool,
                             const config::Settings& settings,
                             std::unique_ptr<IToolTimeoutPolicy> timeout_policy)
    : config_(config), services_(services),
      tools_(services.default_tools()),
      pool_(std::move(pool)), settings_(settings),
      timeout_policy_(timeout_policy ? std::move(timeout_policy)
                                     : std::make_unique<DefaultTimeoutPolicy>()) {}

void ExecutionLoop::add_interceptor(std::unique_ptr<IInterceptor> interceptor) {
    log::debug_fmt("ExecutionLoop: add interceptor: {}", interceptor->name());
    interceptors_.push_back(std::move(interceptor));
}

void ExecutionLoop::log_interceptor_chain() const {
    if (interceptors_.empty()) {
        log::debug_fmt("ExecutionLoop: no interceptors registered");
        return;
    }
    std::string names;
    for (size_t i = 0; i < interceptors_.size(); ++i) {
        if (i > 0) names += " -> ";
        names += interceptors_[i]->name();
    }
    log::debug_fmt("ExecutionLoop: interceptor chain: {}", names);
}

net::Task<llm::ChatResult> ExecutionLoop::run(
    net::EventLoop& loop,
    llm::ConversationHistory& history, base::EventBus& event_bus,
    const net::CancellationToken& cancel) {
    return run(loop, history, event_bus, cancel, tools_);
}

net::Task<llm::ChatResult> ExecutionLoop::run(
    net::EventLoop& loop,
    llm::ConversationHistory& history, base::EventBus& event_bus,
    const net::CancellationToken& cancel,
    const capabilities::tool::ToolRegistry& tool_override) {
    log_interceptor_chain();
    if (settings_.llm.stream) {
        co_return co_await run_stream(loop, history, event_bus, cancel, tool_override);
    }
    co_return co_await run_sync(loop, history, event_bus, cancel, tool_override);
}

// ─── 工具执行（共享逻辑）───────────────────────────────────────────────

void ExecutionLoop::apply_before_tools(
    std::vector<acp::ToolCallRequest>& calls,
    std::vector<acp::ToolCallResult>& blocked,
    const llm::ConversationHistory& history,
    LoopSnapshot& snapshot) {
    for (auto& ic : interceptors_) {
        ic->before_tools(calls, blocked, history, snapshot);
    }
}

std::vector<acp::ToolCallResult> ExecutionLoop::execute_tools(
    std::vector<acp::ToolCallRequest>& calls,
    const capabilities::tool::ToolRegistry& tool_reg,
    llm::ConversationHistory& history,
    base::EventBus& event_bus,
    int step,
    int total_calls) {

    // 1. 应用拦截器过滤
    LoopSnapshot snapshot{
        .event_bus = event_bus,
        .step = step,
        .total_calls = total_calls,
        .max_steps = config_.max_steps,
        .max_calls = config_.max_calls,
        .loop_start = std::chrono::steady_clock::now()
    };
    std::vector<acp::ToolCallResult> blocked;
    apply_before_tools(calls, blocked, history, snapshot);

    // 2. 通知被拦截的工具
    for (const auto& r : blocked) {
        event_bus.publish(agent::ToolBlockedEvent{
            std::string(r.name.data(), r.name.size()),
                        std::string(r.output.data(), r.output.size())});
    }

    // 3. 构建 ToolCallManager
    ToolCallManager tool_mgr(tool_reg, pool_, timeout_policy_->default_timeout());
    for (const auto& c : calls) {
        std::string name(c.name.data(), c.name.size());
        auto timeout = timeout_policy_->get_timeout(name);
        if (timeout.count() > 0) {
            tool_mgr.set_tool_timeout(name, timeout);
        }
    }

    // 4. 通知允许的工具调用（执行前）
    for (const auto& c : calls) {
        agent::ToolCallEvent ev;
        ev.name = std::string(c.name.data(), c.name.size());
        ev.args_json = c.arguments.is_object() ? c.arguments.dump() : "{}";
        ev.tool_call_id = c.id;
        event_bus.publish(ev);
    }

    // 5. 并行执行
    std::vector<acp::ToolCallResult> results;
    if (config_.max_parallel_tools > 0 &&
        static_cast<size_t>(config_.max_parallel_tools) < calls.size()) {
        for (size_t i = 0; i < calls.size(); i += config_.max_parallel_tools) {
            size_t end = std::min(i + static_cast<size_t>(config_.max_parallel_tools), calls.size());
            std::vector<acp::ToolCallRequest> batch(
                calls.begin() + i, calls.begin() + end);
            auto batch_results = tool_mgr.execute_tools_parallel(batch);
            results.insert(results.end(),
                std::make_move_iterator(batch_results.begin()),
                std::make_move_iterator(batch_results.end()));
        }
    } else {
        results = tool_mgr.execute_tools_parallel(calls);
    }

    // 6. 通知结果
    for (const auto& r : results) {
        agent::ToolResultEvent ev;
        ev.name = std::string(r.name.data(), r.name.size());
        ev.output = std::string(r.output.data(), r.output.size());
        ev.tool_call_id = r.tool_call_id;
        ev.ok = r.success;
        event_bus.publish(ev);
    }
    // 被拦截的工具结果也需要通知（它们也是"结果"）
    for (const auto& r : blocked) {
        agent::ToolResultEvent ev;
        ev.name = std::string(r.name.data(), r.name.size());
        ev.output = std::string(r.output.data(), r.output.size());
        ev.tool_call_id = r.tool_call_id;
        ev.ok = r.success;
        if (!r.success) ev.error_message = std::string(r.output.data(), r.output.size());
        event_bus.publish(ev);
    }

    // 7. 先添加 blocked results 到历史（保持协议完整）
    for (const auto& r : blocked)
        history.add_tool_result(r.tool_call_id, r.name, r.output);
    // 再添加实际执行结果
    for (const auto& r : results)
        history.add_tool_result(r.tool_call_id, r.name, r.output);

    return results;
}

// ─── 流式路径 ──────────────────────────────────────────────────────────

net::Task<llm::ChatResult> ExecutionLoop::run_stream(
    net::EventLoop& loop,
    llm::ConversationHistory& history, base::EventBus& event_bus,
    const net::CancellationToken& cancel,
    const capabilities::tool::ToolRegistry& tool_reg) {
    int total_calls = 0;

    // 静默修正：max_steps 必须 > 0
    if (config_.max_steps <= 0) config_.max_steps = 1;

    for (int step = 0; step < config_.max_steps; ++step) {
        cancel.throw_if_cancelled();

        // 拦截器：before_llm
        {
            LoopSnapshot snapshot{
                .event_bus = event_bus,
                .step = step,
                .total_calls = total_calls,
                .max_steps = config_.max_steps,
                .max_calls = config_.max_calls,
                .loop_start = std::chrono::steady_clock::now()
            };
            for (auto& ic : interceptors_) ic->before_llm(history, snapshot);
        }

        std::string accumulated_text;
        accumulated_text.reserve(4096);
        std::string accumulated_thinking;
        std::vector<llm::StreamToolCallDelta> pending_tools;

        llm::StreamHandlers handlers;
        auto usage_tracker = std::make_shared<llm::TokenUsage>();
        handlers.usage_out = usage_tracker;
        int completion_token_count = 0;  // 本次响应累计输出 token 估算
        handlers.on_token = [&](std::string_view token) {
            ++completion_token_count;
            event_bus.publish(agent::TokenEvent{std::string(token), completion_token_count, false});
            accumulated_text += token;
        };
        handlers.on_usage = [&](const llm::TokenUsage& /*usage*/) {
            // LLM 返回实时 usage 时仅计数，不单独发布（ResponseStatsEvent 中已包含）
        };
        handlers.on_thinking = [&](std::string_view token) {
            event_bus.publish(agent::ThinkingEvent{std::string(token)});
            accumulated_thinking += token;
        };
        handlers.on_tool_call = [&](const llm::StreamToolCallDelta& delta) {
            if (static_cast<size_t>(delta.index) >= pending_tools.size())
                pending_tools.resize(delta.index + 1);
            auto& tc = pending_tools[delta.index];
            if (!delta.id.empty()) tc.id = delta.id;
            if (!delta.name.empty()) tc.name = delta.name;
            tc.arguments += delta.arguments;
        };



        // LLM 调用 — 发布 Span 事件用于追踪
        auto llm_start = std::chrono::steady_clock::now();
        uint64_t span_id = reinterpret_cast<uint64_t>(&loop);
        event_bus.publish(agent::SpanStartEvent{span_id, "llm.chat_stream", "llm", llm_start});

        auto result = co_await services_.chat_stream(
            loop, history, tool_reg, {}, std::move(handlers), cancel, {});

        auto llm_end = std::chrono::steady_clock::now();
        auto llm_dur = std::chrono::duration_cast<std::chrono::microseconds>(llm_end - llm_start);
        event_bus.publish(agent::SpanEndEvent{span_id, result.status >= 300,
            result.status >= 300 ? "LLM request failed" : std::string_view{}, llm_dur});

        event_bus.publish(agent::TokenEvent{std::string(), completion_token_count, true});

        // 错误检查
        if (result.status < 200 || result.status >= 300) {
            if (result.is_context_overflow && on_context_overflow_) {
                if (on_context_overflow_(history)) continue;
                co_return llm::ChatResult::context_overflow(
                    std::string("context overflow, recovery failed"));
            }
            co_return llm::ChatResult::error(result.status,
                std::move(result.raw));
        }

        // 无工具调用 — 纯文本
        if (pending_tools.empty()) {
            if (!accumulated_thinking.empty()) event_bus.publish(agent::ThinkingEvent{std::string()});
            history.add_assistant(std::move(accumulated_text));
            event_bus.publish(agent::ResponseStatsEvent{
                .prompt_tokens = result.usage.prompt_tokens,
                .completion_tokens = result.usage.completion_tokens,
                .total_tokens = result.usage.total_tokens,
                .total_seconds = result.latency.total_seconds,
                .ttfb_seconds = result.latency.ttfb_seconds,
                .has_ttfb = result.latency.has_ttfb,
                .model_name = std::string(settings_.llm.model.data(), settings_.llm.model.size()),
                .context_length = settings_.llm.context_length});
            co_return llm::ChatResult::ok(
                history.messages().back().get_all_text(),
                std::move(result.raw));
        }

        std::vector<acp::ToolCallRequest> tool_calls;
        for (auto& tc : pending_tools) {
            if (tc.name.empty()) continue;  // 跳过未完成的工具调用
            acp::ToolCallRequest req;
            req.id = std::move(tc.id);
            req.name = std::move(tc.name);
            req.arguments = Json::parse(
                std::string_view(tc.arguments.data(), tc.arguments.size()));
            tool_calls.push_back(std::move(req));
        }

        // 拦截器：should_stop
        {
            LoopSnapshot snapshot{
                .event_bus = event_bus,
                .step = step,
                .total_calls = total_calls,
                .max_steps = config_.max_steps,
                .max_calls = config_.max_calls,
                .loop_start = std::chrono::steady_clock::now()
            };
            for (auto& ic : interceptors_) {
                auto reason = ic->should_stop(snapshot, history);
                if (!reason.empty()) {
                    co_return llm::ChatResult::tool_limit(
                        config_.max_steps, step + 1, config_.max_calls,
                        total_calls, 50, 0, std::move(reason));
                }
            }
        }

        int budgeted = count_budgeted(tool_calls);
        if (total_calls + budgeted > config_.max_calls) {
            co_return llm::ChatResult::tool_limit(
                config_.max_steps, step + 1, config_.max_calls,
                total_calls, 50, budgeted,
                std::string("Tool call limit reached"));
        }
        total_calls += budgeted;

        // 构建 assistant 消息（工具调用写入历史前）
        auto acp_msg = acp::ACPMessage::assistant_message(std::move(accumulated_text));
        for (const auto& c : tool_calls) acp_msg.add_tool_use(c);
        history.add_message(acp_msg);

        // 执行工具（含拦截器过滤）
        execute_tools(tool_calls, tool_reg, history, event_bus, step, total_calls);

        cancel.throw_if_cancelled();
    }

    co_return llm::ChatResult::tool_limit(
        config_.max_steps, config_.max_steps, config_.max_calls,
        total_calls, 50, 0, std::string("Max steps reached"));
}

// ─── 非流式路径 ────────────────────────────────────────────────────────

net::Task<llm::ChatResult> ExecutionLoop::run_sync(
    net::EventLoop& loop,
    llm::ConversationHistory& history, base::EventBus& event_bus,
    const net::CancellationToken& cancel,
    const capabilities::tool::ToolRegistry& tool_reg) {

    int total_calls = 0;

    // 静默修正：max_steps 必须 > 0
    if (config_.max_steps <= 0) config_.max_steps = 1;

    for (int step = 0; step < config_.max_steps; ++step) {
        cancel.throw_if_cancelled();

        // 拦截器：before_llm
        {
            LoopSnapshot snapshot{
                .event_bus = event_bus,
                .step = step,
                .total_calls = total_calls,
                .max_steps = config_.max_steps,
                .max_calls = config_.max_calls,
                .loop_start = std::chrono::steady_clock::now()
            };
            for (auto& ic : interceptors_) ic->before_llm(history, snapshot);
        }



        auto response = co_await services_.chat_sync(
            loop, history, tool_reg, {}, cancel, {});

        // 错误检查
        bool has_content = false;
        if (response.contains("choices") && response["choices"].is_array() &&
            !response["choices"].empty())
            has_content = true;
        else if (response.contains("content") && response["content"].is_array())
            has_content = true;

        if (!has_content) {
            if (settings_.llm.provider == config::Provider::openai) {
                if (response.contains("error") && response["error"].is_object()) {
                    auto err = response["error"];
                    if (err.value("code", "") == "context_length_exceeded") {
                        if (on_context_overflow_ && on_context_overflow_(history)) continue;
                        co_return llm::ChatResult::context_overflow(
                            std::string("context overflow, recovery failed"));
                    }
                }
            }
            if (llm::detect_context_overflow(response.value("status", 200),
                    std::string_view(response.dump()))) {
                if (on_context_overflow_ && on_context_overflow_(history)) continue;
                co_return llm::ChatResult::context_overflow(
                    std::string("context overflow, recovery failed"));
            }
            std::string error_msg;
            int status = 0;
            if (response.contains("error") && response["error"].is_object()) {
                auto err = response["error"];
                error_msg = err.value("message", "");
                if (err.contains("status")) status = err["status"].get<int>();
            }
            if (error_msg.empty()) error_msg = response.dump();
            co_return llm::ChatResult::error(status > 0 ? status : 500,
                                              std::string(error_msg));
        }

        // ACP 适配器解析响应
        auto acp_response = (settings_.llm.provider == config::Provider::openai)
            ? llm::OpenAIAdapter::from_openai_format(response)
            : llm::AnthropicAdapter::from_anthropic_format(response);

        auto tool_calls = extract_tool_calls(acp_response);
        if (tool_calls.empty()) {
            auto text = extract_text(acp_response);
            auto thinking = extract_thinking(acp_response);
            if (!thinking.empty()) event_bus.publish(agent::ThinkingEvent{std::string(thinking)});
            if (!text.empty()) {
                history.add_assistant(std::string_view(text));
                event_bus.publish(agent::TokenEvent{std::string(text), 0, false});
            }
            auto& tracker = services_.usage_tracker();
            event_bus.publish(agent::ResponseStatsEvent{
                .prompt_tokens = tracker.last_usage().prompt_tokens,
                .completion_tokens = tracker.last_usage().completion_tokens,
                .total_tokens = tracker.last_usage().total_tokens,
                .total_seconds = tracker.last_latency().total_seconds,
                .ttfb_seconds = tracker.last_latency().ttfb_seconds,
                .has_ttfb = tracker.last_latency().has_ttfb,
                .model_name = std::string(settings_.llm.model.data(), settings_.llm.model.size()),
                .context_length = settings_.llm.context_length});
            co_return llm::ChatResult::ok(std::move(text),
                                          response.dump());
        }

        // 拦截器：should_stop + 工具调用限制
        {
            LoopSnapshot snapshot{
                .event_bus = event_bus,
                .step = step,
                .total_calls = total_calls,
                .max_steps = config_.max_steps,
                .max_calls = config_.max_calls,
                .loop_start = std::chrono::steady_clock::now()
            };
            for (auto& ic : interceptors_) {
                auto reason = ic->should_stop(snapshot, history);
                if (!reason.empty()) {
                    co_return llm::ChatResult::tool_limit(
                        config_.max_steps, step + 1, config_.max_calls,
                        total_calls, 50, 0, std::move(reason));
                }
            }
        }

        int budgeted = count_budgeted(tool_calls);
        if (total_calls + budgeted > config_.max_calls) {
            // 超限但仍需通知和执行以保持协议完整
            for (const auto& c : tool_calls) {
                std::string name(c.name.data(), c.name.size());
                std::string args_json = c.arguments.is_object() ? c.arguments.dump() : "{}";
                event_bus.publish(agent::ToolCallEvent{std::move(name), std::move(args_json), c.id});
            }
            ToolCallManager tool_mgr(tool_reg, pool_, timeout_policy_->default_timeout());
            for (const auto& c : tool_calls) {
                auto r = tool_mgr.execute_tool(c);
                agent::ToolResultEvent ev;
                ev.name = std::string(r.name.data(), r.name.size());
                ev.output = std::string(r.output.data(), r.output.size());
                ev.tool_call_id = r.tool_call_id;
                ev.ok = r.success;
                event_bus.publish(ev);
            }
            co_return llm::ChatResult::tool_limit(
                config_.max_steps, step + 1, config_.max_calls,
                total_calls, 50, budgeted,
                std::string("Tool call limit reached"));
        }
        total_calls += budgeted;

        // 构建 assistant 消息
        auto asst_text = extract_text(acp_response);
        auto acp_msg = acp::ACPMessage::assistant_message(std::move(asst_text));
        for (const auto& c : tool_calls) acp_msg.add_tool_use(c);
        history.add_message(acp_msg);

        // 执行工具（含拦截器过滤）
        execute_tools(tool_calls, tool_reg, history, event_bus, step, total_calls);

        cancel.throw_if_cancelled();
    }

    co_return llm::ChatResult::tool_limit(
        config_.max_steps, config_.max_steps, config_.max_calls,
        total_calls, 50, 0, std::string("Max steps reached"));
}

} // namespace ben_gear::agent::execution
