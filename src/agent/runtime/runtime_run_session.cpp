#include "agent/runtime/runtime.hpp"
#include "agent/core/event_sink.hpp"
#include "base/log/logger.hpp"
#include "llm/provider_error.hpp"
#include "llm/stream.hpp"
#include "capabilities/tool/manager.hpp"
#include "acp/core/message.hpp"
#include "llm/adapter.hpp"

using ToolCallManager = ::ben_gear::capabilities::tool::ToolCallManager;

namespace ben_gear::agent::runtime {
/// 从 ACP 消息中提取工具调用请求
std::vector<capabilities::tool::ToolCallRequest> extract_tool_calls(
    const acp::ACPMessage& msg) {
    std::vector<capabilities::tool::ToolCallRequest> calls;
    for (const auto& block : msg.content()) {
        if (block.is_tool_use())
            calls.push_back(block.tool_use());
    }
    return calls;
}

/// 从 ACP 消息中提取文本（跳过思考块）
std::string extract_text(const acp::ACPMessage& msg) {
    std::string text;
    for (const auto& block : msg.content()) {
        if (block.is_text() && !block.is_thinking())
            text.append(block.text());
    }
    return text;
}

/// 从 ACP 消息中提取思考内容
std::string extract_thinking(const acp::ACPMessage& msg) {
    for (const auto& block : msg.content()) {
        if (block.is_thinking())
            return block.text();
    }
    return {};
}

/// 流式执行步骤
static net::Task<llm::ChatResult> run_session_stream(
    net::EventLoop& loop, workspace::Session& session,
    llm::ConversationHistory& history,
    const AgentEventSinks& event_sink,
    const net::CancellationToken& cancel,
    const capabilities::tool::ToolRegistry& tool_reg,
    llm::ProviderClient& provider,
    const std::shared_ptr<base::concurrency::ThreadPool>& core_pool,
    int max_steps, int max_calls, int max_parallel_tools) {

    int total_calls = 0;
    ToolCallManager tool_mgr(tool_reg, core_pool,
                                    std::chrono::seconds(30));
    tool_mgr.set_tool_timeout(std::string("execute_command"), std::chrono::hours(1));
    tool_mgr.set_tool_timeout(std::string("search_files"), std::chrono::seconds(60));
    tool_mgr.set_tool_timeout(std::string("grep_content"), std::chrono::seconds(60));
    for (int step = 0; step < max_steps; ++step) {
        cancel.throw_if_cancelled();

        std::string accumulated_text;
        accumulated_text.reserve(4096);  // 避免流式 token 追加时的多次重分配
        std::string accumulated_thinking;
        std::map<int, llm::StreamToolCallDelta> pending_tools;

        llm::StreamHandlers handlers;
        handlers.on_token = [&](std::string_view token) {
            event_sink.stream.on_token(token);
            accumulated_text += token;
        };
        handlers.on_thinking = [&](std::string_view token) {
            event_sink.stream.on_thinking(token);
            accumulated_thinking += token;
        };
        handlers.on_tool_call = [&](const llm::StreamToolCallDelta& delta) {
            auto& tc = pending_tools[delta.index];
            if (!delta.id.empty()) tc.id = delta.id;
            if (!delta.name.empty()) tc.name = delta.name;
            tc.arguments += delta.arguments;
        };

        // 请求前主动检查上下文 token，必要时触发压缩
        session.maybe_compact(loop, provider, tool_reg);

        auto result = co_await provider.chat_stream_with_tools_async(
            loop, history, tool_reg, {}, std::move(handlers), cancel, {});

        event_sink.stream.on_token("");

        // 检查错误
        if (result.status < 200 || result.status >= 300) {
            if (result.is_context_overflow) {
                if (session.force_compact(loop, provider, tool_reg)) continue;
                co_return llm::ChatResult::context_overflow(
                    std::string("context overflow, recovery failed"));
            }
            co_return llm::ChatResult::error(result.status,
                std::string(result.raw.data(), result.raw.size()));
        }

        // 无工具调用 — 纯文本响应
        if (pending_tools.empty()) {
            if (!accumulated_thinking.empty()) {
                event_sink.stream.on_thinking("");
            }
            history.add_assistant(std::move(accumulated_text));
            event_sink.stream.on_response_stats(result.usage, result.latency, {}, 0);
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

        // 检查限制
        int budgeted = 0;
        for (auto& c : tool_calls) {
            if (std::string_view(c.name.data(), c.name.size()) != "update_todo")
                ++budgeted;
        }
        if (total_calls + budgeted > max_calls) {
            co_return llm::ChatResult::tool_limit(
                max_steps, step + 1, max_calls, total_calls, 50, budgeted,
                std::string("Tool call limit reached"));
        }
        total_calls += budgeted;

        // 通知工具调用
        for (auto& c : tool_calls) event_sink.tool.on_tool_call(c);

        // 执行工具（并行）
        std::vector<capabilities::tool::ToolCallResult> results;
        if (max_parallel_tools > 0 && static_cast<size_t>(max_parallel_tools) < tool_calls.size()) {
            // 分批并行执行，控制并发度
            for (size_t i = 0; i < tool_calls.size(); i += max_parallel_tools) {
                size_t batch_end = std::min(i + max_parallel_tools, tool_calls.size());
                std::vector<capabilities::tool::ToolCallRequest> batch(
                    tool_calls.begin() + i, tool_calls.begin() + batch_end);
                auto batch_results = tool_mgr.execute_tools_parallel(batch);
                results.insert(results.end(),
                    std::make_move_iterator(batch_results.begin()),
                    std::make_move_iterator(batch_results.end()));
            }
        } else {
            results = tool_mgr.execute_tools_parallel(tool_calls);
        }

        // 构建 assistant 消息
        auto acp_msg = acp::ACPMessage::assistant_message(
            std::move(accumulated_text));
        for (auto& c : tool_calls) acp_msg.add_tool_use(c);
        history.add_message(acp_msg);

        for (auto& r : results)
            history.add_tool_result(r.tool_call_id, r.name, r.output);

        // 通知工具结果
        for (auto& r : results) event_sink.tool.on_tool_result(r);

        cancel.throw_if_cancelled();
    }

    co_return llm::ChatResult::tool_limit(
        max_steps, max_steps, max_calls, total_calls, 50, 0,
        std::string("Max steps reached"));
}

net::Task<llm::ChatResult> Runtime::run_session_async(SessionRunConfig config) {
    return run_session_async(config.loop, config.session, std::move(config.prompt),
                             config.event_sink, config.cancel, config.tool_override);
}

net::Task<llm::ChatResult> Runtime::run_session_async(
    net::EventLoop& loop, workspace::Session& session,
    std::string prompt, const AgentEventSinks& event_sink,
    const net::CancellationToken& cancel,
    const capabilities::tool::ToolRegistry* tool_override) {

    const capabilities::tool::ToolRegistry& tool_reg = tool_override ? *tool_override : tools_.registry_;
    auto& history = session.history();

    // 构建系统提示 — 包含 SOUL/RULES/USER/MEMORY/skills
    auto sys_prompt = memory_.builder_->build();
    history.set_system_prompt(sys_prompt);
    history.add_user(std::string_view(prompt.data(), prompt.size()));

    // 流式模式
    if (settings_.stream) {
        co_return co_await run_session_stream(
            loop, session, history, event_sink, cancel, tool_reg,
            provider_, infra_.core_pool,
            max_tool_steps_ > 0 ? max_tool_steps_ : 20,
            max_tool_calls_ > 0 ? max_tool_calls_ : 50,
            max_parallel_tools_);
    }

    const int max_steps = max_tool_steps_ > 0 ? max_tool_steps_ : 20;
    const int max_calls = max_tool_calls_ > 0 ? max_tool_calls_ : 50;
    int total_calls = 0;

    ToolCallManager tool_mgr(tool_reg, infra_.core_pool,
                                    std::chrono::seconds(30));
    tool_mgr.set_tool_timeout(std::string("execute_command"), std::chrono::hours(1));
    tool_mgr.set_tool_timeout(std::string("search_files"), std::chrono::seconds(60));
    tool_mgr.set_tool_timeout(std::string("grep_content"), std::chrono::seconds(60));

    for (int step = 0; step < max_steps; ++step) {
        cancel.throw_if_cancelled();

        session.maybe_compact(loop, provider_, tool_reg);

        auto response = co_await provider_.chat_with_tools_async(
            loop, history, tool_reg, {}, cancel, {});

        // 检查错误
        bool has_content = false;
        if (response.contains("choices") && response["choices"].is_array() && !response["choices"].empty())
            has_content = true;
        else if (response.contains("content") && response["content"].is_array())
            has_content = true;

        if (!has_content) {
            // 检查上下文溢出
            if (settings_.provider == config::Provider::openai) {
                if (response.contains("error") && response["error"].is_object()) {
                    auto err = response["error"];
                    if (err.value("code", "") == "context_length_exceeded") {
                        if (session.force_compact(loop, provider_, tool_reg)) continue;
                        co_return llm::ChatResult::context_overflow(
                            std::string("context overflow, recovery failed"));
                    }
                }
            }
            if (llm::detect_context_overflow(response.value("status", 200),
                    std::string_view(response.dump()))) {
                if (session.force_compact(loop, provider_, tool_reg)) continue;
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

        // 通过 ACP 适配器统一解析提供商响应
        auto acp_response = (settings_.provider == config::Provider::openai)
            ? llm::OpenAIAdapter::from_openai_format(response)
            : llm::AnthropicAdapter::from_anthropic_format(response);

        auto tool_calls = extract_tool_calls(acp_response);
        if (tool_calls.empty()) {
            // 纯文本响应
            auto text = extract_text(acp_response);
            auto thinking = extract_thinking(acp_response);
            if (!thinking.empty()) event_sink.stream.on_thinking(thinking);
            if (!text.empty()) {
                history.add_assistant(std::string_view(text));
                event_sink.stream.on_token(text);
            }

            auto& tracker = provider_.usage_tracker();
            event_sink.stream.on_response_stats(tracker.last_usage(), tracker.last_latency(),
                                         std::string_view(settings_.model.data(),
                                                          settings_.model.size()),
                                         settings_.context_length);
            co_return llm::ChatResult::ok(std::string(text),
                                          std::string(response.dump()));
        }

        // 检查工具调用限制 — 排除 update_todo（内部管理工具），与流式模式一致
        int budgeted = 0;
        for (const auto& c : tool_calls) {
            if (std::string_view(c.name.data(), c.name.size()) != "update_todo")
                ++budgeted;
        }
        if (total_calls + budgeted > max_calls) {
            for (const auto& c : tool_calls) event_sink.tool.on_tool_call(c);
            for (const auto& c : tool_calls)
                event_sink.tool.on_tool_result(tool_mgr.execute_tool(c));
            co_return llm::ChatResult::tool_limit(
                max_steps, step + 1, max_calls, total_calls, 50, budgeted,
                std::string("Tool call limit reached"));
        }
        total_calls += budgeted;
        for (const auto& c : tool_calls) event_sink.tool.on_tool_call(c);

        // 执行工具（并行）
        std::vector<capabilities::tool::ToolCallResult> results;
        if (max_parallel_tools_ > 0 && static_cast<size_t>(max_parallel_tools_) < tool_calls.size()) {
            // 分批并行执行，控制并发度
            for (size_t i = 0; i < tool_calls.size(); i += max_parallel_tools_) {
                size_t batch_end = std::min(i + static_cast<size_t>(max_parallel_tools_), tool_calls.size());
                std::vector<capabilities::tool::ToolCallRequest> batch(
                    tool_calls.begin() + i, tool_calls.begin() + batch_end);
                auto batch_results = tool_mgr.execute_tools_parallel(batch);
                results.insert(results.end(),
                    std::make_move_iterator(batch_results.begin()),
                    std::make_move_iterator(batch_results.end()));
            }
        } else {
            results = tool_mgr.execute_tools_parallel(tool_calls);
        }

        for (const auto& r : results) event_sink.tool.on_tool_result(r);

        // 添加到历史
        auto asst_text = extract_text(acp_response);
        auto acp_msg = acp::ACPMessage::assistant_message(
            std::move(asst_text));
        for (const auto& c : tool_calls) acp_msg.add_tool_use(c);
        history.add_message(acp_msg);
        for (const auto& r : results)
            history.add_tool_result(r.tool_call_id, r.name, r.output);

        cancel.throw_if_cancelled();
    }

    co_return llm::ChatResult::tool_limit(
        max_steps, max_steps, max_calls, total_calls, 50, 0,
        std::string("Max steps reached"));
}

} // namespace ben_gear::agent::runtime

