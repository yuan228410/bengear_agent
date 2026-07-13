#include "agent/runtime/runtime.hpp"
#include "agent/core/interface/event_sink.hpp"
#include "base/log/logger.hpp"
#include "llm/provider_error.hpp"
#include "llm/stream.hpp"
#include "tool/manager.hpp"
#include "tool/acp/core/message.hpp"

using ToolCallManager = ::ben_gear::llm::ToolCallManager;

namespace ben_gear::agent::runtime {

namespace {

/// 从 JSON 响应中提取工具调用请求
std::vector<llm::ToolCallRequest> extract_tool_calls(Json& response, config::Provider provider) {
    std::vector<llm::ToolCallRequest> calls;
    if (provider == config::Provider::openai) {
        if (!response.contains("choices") || !response["choices"].is_array() || response["choices"].empty())
            return calls;
        auto msg = response["choices"][0]["message"];
        if (!msg.contains("tool_calls") || !msg["tool_calls"].is_array())
            return calls;
        auto tool_calls = msg["tool_calls"];
        for (size_t i = 0; i < tool_calls.size(); ++i) {
            auto tc = tool_calls[i];
            llm::ToolCallRequest req;
            auto id_str = Json(tc["id"]).get<std::string>();
            auto name_str = Json(tc["function"]["name"]).get<std::string>();
            req.id = container::String(id_str.data(), id_str.size());
            req.name = container::String(name_str.data(), name_str.size());
            auto args_json = Json(tc["function"]["arguments"]);
            try { req.arguments = Json::parse(args_json.get<std::string>()); } catch (...) { req.arguments = Json::object(); }
            calls.push_back(std::move(req));
        }
    } else {
        // Anthropic
        if (!response.contains("content") || !response["content"].is_array())
            return calls;
        auto content = response["content"];
        for (size_t i = 0; i < content.size(); ++i) {
            auto block = content[i];
            if (Json(block["type"]).get<std::string>() != "tool_use") continue;
            llm::ToolCallRequest req;
            auto id_str = Json(block["id"]).get<std::string>();
            auto name_str = Json(block["name"]).get<std::string>();
            req.id = container::String(id_str.data(), id_str.size());
            req.name = container::String(name_str.data(), name_str.size());
            req.arguments = block.value("input", Json::object());
            calls.push_back(std::move(req));
        }
    }
    return calls;
}

/// 从 JSON 响应中提取文本
container::String extract_text(Json& response, config::Provider provider) {
    if (provider == config::Provider::openai) {
        if (response.contains("choices") && response["choices"].is_array() && !response["choices"].empty()) {
            auto msg = response["choices"][0]["message"];  // ProxyRef (non-const)
            if (msg.contains("content") && !msg["content"].is_null()) {
                auto text = Json(msg["content"]).get<std::string>();
                return container::String(text.data(), text.size());
            }
        }
    } else {
        if (response.contains("content") && response["content"].is_array()) {
            auto content = response["content"];
            container::String text;
            for (size_t i = 0; i < content.size(); ++i) {
                auto block = content[i];
                if (block.contains("type") && Json(block["type"]).get<std::string>() == "text") {
                    auto part = Json(block["text"]).get<std::string>();
                    text.append(part.data(), part.size());
                }
            }
            return text;
        }
    }
    return {};
}

/// 从 JSON 响应中提取思考内容
container::String extract_thinking(Json& response, config::Provider provider) {
    if (provider == config::Provider::openai) {
        if (response.contains("choices") && response["choices"].is_array() && !response["choices"].empty()) {
            auto msg = response["choices"][0]["message"];
            if (msg.contains("reasoning_content") && !msg["reasoning_content"].is_null()) {
                auto reasoning = Json(msg["reasoning_content"]).get<std::string>();
                return container::String(reasoning.data(), reasoning.size());
            }
        }
    } else {
        if (response.contains("content") && response["content"].is_array()) {
            auto content = response["content"];
            for (size_t i = 0; i < content.size(); ++i) {
                auto block = content[i];
                if (Json(block["type"]).get<std::string>() == "thinking" && block.contains("thinking")) {
                    auto thinking = Json(block["thinking"]).get<std::string>();
                    return container::String(thinking.data(), thinking.size());
                }
            }
        }
    }
    return {};
}

} // namespace

/// 流式执行步骤
static net::Task<llm::ChatResult> run_session_stream(
    net::EventLoop& loop, workspace::Session& session,
    workspace::ConversationHistory& history,
    const AgentEventSink& event_sink,
    const net::CancellationToken& cancel,
    const llm::ToolRegistry& tool_reg,
    llm::ProviderClient& provider,
    const std::shared_ptr<base::concurrency::ThreadPool>& core_pool,
    const permission::ToolPermissionProvider* permission_provider,
    int max_steps, int max_calls) {

    int total_calls = 0;
    ToolCallManager tool_mgr(tool_reg, core_pool,
                                    std::chrono::seconds(30));
    for (int step = 0; step < max_steps; ++step) {
        cancel.throw_if_cancelled();

        container::String accumulated_text;
        accumulated_text.reserve(4096);  // 避免流式 token 追加时的多次重分配
        container::String accumulated_thinking;
        std::map<int, llm::StreamToolCallDelta> pending_tools;

        llm::StreamHandlers handlers;
        handlers.on_token = [&](std::string_view token) {
            event_sink.on_token(token);
            accumulated_text += token;
        };
        handlers.on_thinking = [&](std::string_view token) {
            event_sink.on_thinking(token);
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

        event_sink.on_token("");

        // 检查错误
        if (result.status < 200 || result.status >= 300) {
            if (result.is_context_overflow) {
                if (session.force_compact(loop, provider, tool_reg)) continue;
                co_return llm::ChatResult::context_overflow(
                    container::String("context overflow, recovery failed"));
            }
            co_return llm::ChatResult::error(result.status,
                container::String(result.raw.data(), result.raw.size()));
        }

        // 无工具调用 — 纯文本响应
        if (pending_tools.empty()) {
            if (!accumulated_thinking.empty()) {
                event_sink.on_thinking("");
            }
            history.add_assistant(std::move(accumulated_text));
            event_sink.on_response_stats(result.usage, result.latency, {}, 0);
            co_return llm::ChatResult::ok(
                container::String(history.messages().back().get_all_text()),
                container::String(result.raw.data(), result.raw.size()));
        }

        // 解析工具调用
        std::vector<llm::ToolCallRequest> tool_calls;
        for (auto& [idx, tc] : pending_tools) {
            llm::ToolCallRequest req;
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
                container::String("Tool call limit reached"));
        }
        total_calls += budgeted;

        // 通知工具调用
        for (auto& c : tool_calls) event_sink.on_tool_call(c);

        // 执行工具
        std::vector<llm::ToolCallResult> results;
        for (auto& c : tool_calls)
            results.push_back(tool_mgr.execute_tool(c));

        // 构建 assistant 消息
        auto acp_msg = acp::ACPMessage::assistant_message(
            std::move(accumulated_text));
        for (auto& c : tool_calls) acp_msg.add_tool_use(c);
        history.add_message(acp_msg);

        for (auto& r : results)
            history.add_tool_result(r.tool_call_id, r.name, r.output);

        // 通知工具结果
        for (auto& r : results) event_sink.on_tool_result(r);

        cancel.throw_if_cancelled();
    }

    co_return llm::ChatResult::tool_limit(
        max_steps, max_steps, max_calls, total_calls, 50, 0,
        container::String("Max steps reached"));
}

net::Task<llm::ChatResult> Runtime::run_session_async(SessionRunConfig config) {
    return run_session_async(config.loop, config.session, std::move(config.prompt),
                             config.event_sink, config.cancel, config.tool_override);
}

net::Task<llm::ChatResult> Runtime::run_session_async(
    net::EventLoop& loop, workspace::Session& session,
    container::String prompt, const AgentEventSink& event_sink,
    const net::CancellationToken& cancel, const llm::ToolRegistry* tool_override) {

    const llm::ToolRegistry& tool_reg = tool_override ? *tool_override : tools_;
    auto& history = session.history();

    // 构建系统提示 — 包含 SOUL/RULES/USER/MEMORY/skills
    auto sys_prompt = context_builder_->build();
    history.set_system_prompt(sys_prompt);
    history.add_user(std::string_view(prompt.data(), prompt.size()));

    // 流式模式
    if (settings_.stream) {
        co_return co_await run_session_stream(
            loop, session, history, event_sink, cancel, tool_reg,
            provider_, core_pool_, this,
            max_tool_steps_ > 0 ? max_tool_steps_ : 20,
            max_tool_calls_ > 0 ? max_tool_calls_ : 50);
    }

    const int max_steps = max_tool_steps_ > 0 ? max_tool_steps_ : 20;
    const int max_calls = max_tool_calls_ > 0 ? max_tool_calls_ : 50;
    int total_calls = 0;

    ToolCallManager tool_mgr(tool_reg, core_pool_,
                                    std::chrono::seconds(30),
                                    shared_from_this());

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
                            container::String("context overflow, recovery failed"));
                    }
                }
            }
            if (llm::detect_context_overflow(response.value("status", 200),
                    std::string_view(response.dump()))) {
                if (session.force_compact(loop, provider_, tool_reg)) continue;
                co_return llm::ChatResult::context_overflow(
                    container::String("context overflow, recovery failed"));
            }
            container::String error_msg;
            int status = 0;
            if (response.contains("error") && response["error"].is_object()) {
                auto err = response["error"];
                error_msg = err.value("message", "");
                if (err.contains("status")) status = err["status"].get<int>();
            }
            if (error_msg.empty()) error_msg = response.dump();
            co_return llm::ChatResult::error(status > 0 ? status : 500,
                                              container::String(error_msg));
        }

        auto tool_calls = extract_tool_calls(response, settings_.provider);
        if (tool_calls.empty()) {
            // 纯文本响应
            auto text = extract_text(response, settings_.provider);
            auto thinking = extract_thinking(response, settings_.provider);
            if (!thinking.empty()) event_sink.on_thinking(thinking);
            if (!text.empty()) {
                history.add_assistant(std::string_view(text));
                event_sink.on_token(text);
            }

            auto& tracker = provider_.usage_tracker();
            event_sink.on_response_stats(tracker.last_usage(), tracker.last_latency(),
                                         std::string_view(settings_.model.data(),
                                                          settings_.model.size()),
                                         settings_.context_length);
            co_return llm::ChatResult::ok(container::String(text),
                                          container::String(response.dump()));
        }

        // 检查工具调用限制
        for (const auto& c : tool_calls) event_sink.on_tool_call(c);

        std::vector<llm::ToolCallResult> results;
        for (const auto& c : tool_calls)
            results.push_back(tool_mgr.execute_tool(c));

        for (const auto& r : results) event_sink.on_tool_result(r);

        // 添加到历史
        auto asst_text = extract_text(response, settings_.provider);
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
        container::String("Max steps reached"));
}

} // namespace ben_gear::agent::runtime

