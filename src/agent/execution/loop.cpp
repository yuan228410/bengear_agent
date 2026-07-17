#include "agent/execution/loop.hpp"

#include "acp/core/message.hpp"
#include "base/log/logger.hpp"
#include "capabilities/tool/manager.hpp"
#include "llm/adapter.hpp"
#include "llm/provider_error.hpp"
#include "llm/stream.hpp"

namespace ben_gear::agent::execution {

using ToolCallManager = capabilities::tool::ToolCallManager;

// ─── 助手函数 ────────────────────────────────────────────────────────

namespace {

std::vector<capabilities::tool::ToolCallRequest> extract_tool_calls(
    const acp::ACPMessage& msg) {
    std::vector<capabilities::tool::ToolCallRequest> calls;
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
int count_budgeted(const std::vector<capabilities::tool::ToolCallRequest>& calls) {
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
                             llm::ProviderClient& provider,
                             const capabilities::tool::ToolRegistry& tools,
                             std::shared_ptr<base::concurrency::ThreadPool> pool,
                             const config::Settings& settings)
    : config_(config), provider_(provider), tools_(tools),
      pool_(std::move(pool)), settings_(settings) {}

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
    llm::ConversationHistory& history, const AgentEventSinks& sinks,
    const net::CancellationToken& cancel) {
    return run(loop, history, sinks, cancel, tools_);
}

net::Task<llm::ChatResult> ExecutionLoop::run(
    net::EventLoop& loop,
    llm::ConversationHistory& history, const AgentEventSinks& sinks,
    const net::CancellationToken& cancel,
    const capabilities::tool::ToolRegistry& tool_override) {
    log_interceptor_chain();
    if (settings_.stream) {
        co_return co_await run_stream(loop, history, sinks, cancel, tool_override);
    }
    co_return co_await run_sync(loop, history, sinks, cancel, tool_override);
}

// ─── 工具执行（共享逻辑）───────────────────────────────────────────────

void ExecutionLoop::apply_before_tools(
    std::vector<capabilities::tool::ToolCallRequest>& calls,
    std::vector<capabilities::tool::ToolCallResult>& blocked,
    const llm::ConversationHistory& history,
    InterceptorContext& ctx) {
    for (auto& ic : interceptors_) {
        ic->before_tools(calls, blocked, history, ctx);
    }
}

std::vector<capabilities::tool::ToolCallResult> ExecutionLoop::execute_tools(
    std::vector<capabilities::tool::ToolCallRequest>& calls,
    const capabilities::tool::ToolRegistry& tool_reg,
    llm::ConversationHistory& history,
    const AgentEventSinks& sinks) {

    // 1. 应用拦截器过滤
    InterceptorContext ctx{sinks};
    std::vector<capabilities::tool::ToolCallResult> blocked;
    apply_before_tools(calls, blocked, history, ctx);

    // 2. 通知被拦截的工具
    for (const auto& r : blocked) {
        sinks.tool.on_tool_blocked(
            std::string_view(r.name.data(), r.name.size()),
            std::string_view(r.output.data(), r.output.size()));
    }

    // 3. 构建 ToolCallManager
    ToolCallManager tool_mgr(tool_reg, pool_, tool_timeout_default_);
    tool_mgr.set_tool_timeout(std::string("execute_command"), tool_timeout_exec_cmd_);
    tool_mgr.set_tool_timeout(std::string("search_files"), tool_timeout_search_);
    tool_mgr.set_tool_timeout(std::string("grep_content"), tool_timeout_search_);

    // 4. 通知允许的工具调用（执行前）
    for (const auto& c : calls) sinks.tool.on_tool_call(c);

    // 5. 并行执行
    std::vector<capabilities::tool::ToolCallResult> results;
    if (config_.max_parallel_tools > 0 &&
        static_cast<size_t>(config_.max_parallel_tools) < calls.size()) {
        for (size_t i = 0; i < calls.size(); i += config_.max_parallel_tools) {
            size_t end = std::min(i + static_cast<size_t>(config_.max_parallel_tools), calls.size());
            std::vector<capabilities::tool::ToolCallRequest> batch(
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
    for (const auto& r : results) sinks.tool.on_tool_result(r);
    // 被拦截的工具结果也需要通知（它们也是"结果"）
    for (const auto& r : blocked) sinks.tool.on_tool_result(r);

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
    llm::ConversationHistory& history, const AgentEventSinks& sinks,
    const net::CancellationToken& cancel,
    const capabilities::tool::ToolRegistry& tool_reg) {
    int total_calls = 0;

    for (int step = 0; step < config_.max_steps; ++step) {
        cancel.throw_if_cancelled();

        // 拦截器：before_llm
        {
            InterceptorContext ctx{sinks};
            for (auto& ic : interceptors_) ic->before_llm(history, ctx);
        }

        std::string accumulated_text;
        accumulated_text.reserve(4096);
        std::string accumulated_thinking;
        std::map<int, llm::StreamToolCallDelta> pending_tools;

        llm::StreamHandlers handlers;
        handlers.on_token = [&](std::string_view token) {
            sinks.stream.on_token(token);
            accumulated_text += token;
        };
        handlers.on_thinking = [&](std::string_view token) {
            sinks.stream.on_thinking(token);
            accumulated_thinking += token;
        };
        handlers.on_tool_call = [&](const llm::StreamToolCallDelta& delta) {
            auto& tc = pending_tools[delta.index];
            if (!delta.id.empty()) tc.id = delta.id;
            if (!delta.name.empty()) tc.name = delta.name;
            tc.arguments += delta.arguments;
        };



        auto result = co_await provider_.chat_stream_with_tools_async(
            loop, history, tool_reg, {}, std::move(handlers), cancel, {});

        sinks.stream.on_token("");

        // 错误检查
        if (result.status < 200 || result.status >= 300) {
            if (result.is_context_overflow && on_context_overflow_) {
                if (on_context_overflow_(history)) continue;
                co_return llm::ChatResult::context_overflow(
                    std::string("context overflow, recovery failed"));
            }
            co_return llm::ChatResult::error(result.status,
                std::string(result.raw.data(), result.raw.size()));
        }

        // 无工具调用 — 纯文本
        if (pending_tools.empty()) {
            if (!accumulated_thinking.empty()) sinks.stream.on_thinking("");
            history.add_assistant(std::move(accumulated_text));
            sinks.stream.on_response_stats(result.usage, result.latency, {}, 0);
            co_return llm::ChatResult::ok(
                std::string(history.messages().back().get_all_text()),
                std::string(result.raw.data(), result.raw.size()));
        }

        // 解析工具调用
        std::vector<capabilities::tool::ToolCallRequest> tool_calls;
        for (auto& [idx, tc] : pending_tools) {
            capabilities::tool::ToolCallRequest req;
            req.id = std::move(tc.id);
            req.name = std::move(tc.name);
            req.arguments = Json::parse(
                std::string_view(tc.arguments.data(), tc.arguments.size()));
            tool_calls.push_back(std::move(req));
        }

        // 拦截器：should_stop
        {
            for (auto& ic : interceptors_) {
                auto reason = ic->should_stop(step, total_calls, history);
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
        execute_tools(tool_calls, tool_reg, history, sinks);

        cancel.throw_if_cancelled();
    }

    co_return llm::ChatResult::tool_limit(
        config_.max_steps, config_.max_steps, config_.max_calls,
        total_calls, 50, 0, std::string("Max steps reached"));
}

// ─── 非流式路径 ────────────────────────────────────────────────────────

net::Task<llm::ChatResult> ExecutionLoop::run_sync(
    net::EventLoop& loop,
    llm::ConversationHistory& history, const AgentEventSinks& sinks,
    const net::CancellationToken& cancel,
    const capabilities::tool::ToolRegistry& tool_reg) {

    int total_calls = 0;

    for (int step = 0; step < config_.max_steps; ++step) {
        cancel.throw_if_cancelled();

        // 拦截器：before_llm
        {
            InterceptorContext ctx{sinks};
            for (auto& ic : interceptors_) ic->before_llm(history, ctx);
        }



        auto response = co_await provider_.chat_with_tools_async(
            loop, history, tool_reg, {}, cancel, {});

        // 错误检查
        bool has_content = false;
        if (response.contains("choices") && response["choices"].is_array() &&
            !response["choices"].empty())
            has_content = true;
        else if (response.contains("content") && response["content"].is_array())
            has_content = true;

        if (!has_content) {
            if (settings_.provider == config::Provider::openai) {
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
        auto acp_response = (settings_.provider == config::Provider::openai)
            ? llm::OpenAIAdapter::from_openai_format(response)
            : llm::AnthropicAdapter::from_anthropic_format(response);

        auto tool_calls = extract_tool_calls(acp_response);
        if (tool_calls.empty()) {
            auto text = extract_text(acp_response);
            auto thinking = extract_thinking(acp_response);
            if (!thinking.empty()) sinks.stream.on_thinking(thinking);
            if (!text.empty()) {
                history.add_assistant(std::string_view(text));
                sinks.stream.on_token(text);
            }
            auto& tracker = provider_.usage_tracker();
            sinks.stream.on_response_stats(tracker.last_usage(), tracker.last_latency(),
                                     std::string_view(settings_.model.data(),
                                                      settings_.model.size()),
                                     settings_.context_length);
            co_return llm::ChatResult::ok(std::string(text),
                                          std::string(response.dump()));
        }

        // 拦截器：should_stop + 工具调用限制
        {
            for (auto& ic : interceptors_) {
                auto reason = ic->should_stop(step, total_calls, history);
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
            for (const auto& c : tool_calls) sinks.tool.on_tool_call(c);
            ToolCallManager tool_mgr(tool_reg, pool_, std::chrono::seconds(30));
            for (const auto& c : tool_calls)
                sinks.tool.on_tool_result(tool_mgr.execute_tool(c));
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
        execute_tools(tool_calls, tool_reg, history, sinks);

        cancel.throw_if_cancelled();
    }

    co_return llm::ChatResult::tool_limit(
        config_.max_steps, config_.max_steps, config_.max_calls,
        total_calls, 50, 0, std::string("Max steps reached"));
}

} // namespace ben_gear::agent::execution
