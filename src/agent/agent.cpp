#include "ben_gear/agent/agent_impl.hpp"
#include "ben_gear/llm/provider_error.hpp"

#include <map>
#include <vector>

namespace ben_gear::agent {

/// 上下文溢出恢复
static bool recover_from_overflow(workspace::Session& session,
                                  net::EventLoop& loop,
                                  llm::ProviderClient& provider,
                                  const llm::ToolRegistry& tools) {
    log::info_fmt("context_overflow: starting recovery (history_size={})", session.history().size());
    bool ok = session.force_compact(loop, provider, tools);
    if (!ok) {
        log::error_fmt("context_overflow: recovery failed, context still over limit");
    }
    return ok;
}

net::Task<llm::ChatResult> Agent::run_session_async(net::EventLoop& loop,
                                                     workspace::Session& session,
                                                     base::container::String prompt,
                                                     const AgentCallbacks& callbacks,
                                                     const net::CancellationToken& cancel) {
    if (prompt.empty()) {
        log::error_fmt("agent: invalid prompt (empty)");
        co_return llm::ChatResult::error(400, container::String("Invalid input: prompt is empty"));
    }

    auto& history = session.history();

    log::info_fmt("agent session started: session_id={}, stream={}, prompt_len={}",
                  std::string_view(session.session_id().data(), session.session_id().size()),
                  resources_->settings().stream, prompt.size());
    if (history.empty()) {
        history.add_system(std::string_view(AgentImpl::build_system_prompt(*resources_)));
    }

    // 计划模式：注入专用系统提示（避免重复注入）
    if (plan_manager_.in_plan_mode()) {
        if (!plan_manager_.is_prompt_injected()) {
            history.add_system(AgentImpl::kPlanModePrompt);
            plan_manager_.mark_prompt_injected();
        }
    }

    // 添加用户消息到 history
    history.add_user(std::string_view(prompt.data(), prompt.size()));

    if (resources_->settings().stream) {
        co_return co_await run_session_stream_step(loop, session, history,
                                                    std::string_view(prompt.data(), prompt.size()), callbacks, cancel);
    }

    // 非流式路径
    auto prompt_copy = std::string(prompt.data(), prompt.size());

    for (int step = 0; step < resources_->max_tool_steps(); ++step) {
        cancel.throw_if_cancelled();
        log::info_fmt("agent non-stream step {}/{}: sending request", step + 1, resources_->max_tool_steps());

        auto response = co_await resources_->provider().chat_with_tools_async(loop, history, resources_->tools());

        log::debug_fmt("agent: received response, type={}", response.type_name());

        // 检查是否为有效的 LLM 响应
        bool has_content = false;
        if (response.contains("choices") && response["choices"].is_array() && !response["choices"].empty()) {
            has_content = true;
        } else if (response.contains("content") && response["content"].is_array()) {
            has_content = true;
        }
        if (!has_content) {
            std::string error_msg;
            int status = 0;
            if (response.contains("error") && response["error"].is_object()) {
                Json err = response["error"];
                error_msg = err.value("message", "");
                if (err.contains("status")) status = err["status"].get<int>();
            }
            if (error_msg.empty()) error_msg = response.dump();

            if (llm::detect_context_overflow(status, error_msg)) {
                log::info_fmt("agent: context_overflow detected (non-stream), status={}", status);
                if (recover_from_overflow(session, loop, resources_->provider(), resources_->tools())) {
                    continue;
                }
                co_return llm::ChatResult::error(400, container::String("上下文超限，压缩恢复失败"));
            }

            log::error_fmt("agent non-stream invalid response: status={}", status);
            co_return llm::ChatResult::error(status > 0 ? status : 500, std::move(error_msg));
        }

        if (!llm::ToolCallManager::has_tool_calls(response, resources_->settings().provider)) {
            log::debug_fmt("agent: no tool calls, extracting text");
            AgentImpl::emit_thinking(response, callbacks, resources_->settings().provider);
            auto text = AgentImpl::extract_response_text(response, resources_->settings().provider);
            log::info_fmt("agent: extracted text, size={}", text.size());

            auto thinking_text = AgentImpl::extract_thinking(response, resources_->settings().provider);
            if (!thinking_text.empty()) {
                session.persist_message(container::String("thinking"),
                                        thinking_text, resources_->history_db());
            }
            history.add_assistant(std::string_view(text));
            callbacks.on_token(text);

            const auto& tracker = resources_->provider().usage_tracker();
            callbacks.on_response_stats(tracker.last_usage(), tracker.last_latency());

            session.persist_message(container::String("user"), std::string_view(prompt_copy), resources_->history_db());
            session.persist_message(container::String("assistant"), std::string_view(text), resources_->history_db());

            session.maybe_compact(loop, resources_->provider(), resources_->tools());

            co_return llm::ChatResult::ok(std::move(text), container::String(response.dump()));
        }

        auto tool_calls = AgentImpl::extract_tool_calls(response, tool_manager_, resources_->settings().provider);

        // plan 模式：硬约束过滤非 read_only 工具
        std::vector<llm::ToolCallRequest> blocked_calls;
        std::vector<llm::ToolCallResult> blocked_results;
        if (plan_manager_.in_plan_mode()) {
            auto filter = resources_->tools().filter_plan_mode_tools(tool_calls);
            // 通知 UI 层工具被拦截
            for (const auto& blocked : filter.blocked_calls) {
                auto name_sv = std::string_view(blocked.name.data(), blocked.name.size());
                callbacks.on_tool_blocked(name_sv, "read-only");
            }
            tool_calls = std::move(filter.allowed);
            blocked_calls = std::move(filter.blocked_calls);
            blocked_results = std::move(filter.blocked_results);
        }

        for (const auto& call : tool_calls) {
            log::info_fmt("tool call started: name={}, id={}, args={}", call.name, call.id, call.arguments.dump());
        }

        auto results = tool_manager_.execute_tools(tool_calls);

        for (const auto& r : results) {
            log::info_fmt("tool call completed: name={}, success={}, output_size={}",
                          r.name, r.success, r.output.size());
        }

        // assistant 消息包含所有 tool_use（allowed + blocked），否则 LLM 协议断裂
        {
            auto all_calls = tool_calls;
            for (const auto& bc : blocked_calls) {
                all_calls.push_back(bc);
            }
            AgentImpl::add_assistant_message_with_tools_to(response, all_calls, history, resources_->settings().provider);
        }

        for (const auto& result : results) {
            history.add_tool_result(result.tool_call_id, result.name, result.output);
        }
        for (const auto& br : blocked_results) {
            history.add_tool_result(br.tool_call_id, br.name, br.output);
        }

        persist_tool_step(session, history, tool_calls, results);

        AgentImpl::emit_thinking(response, callbacks, resources_->settings().provider);

        for (const auto& call : tool_calls) { callbacks.on_tool_call(call); }
        for (const auto& result : results) { callbacks.on_tool_result(result); }
        for (const auto& br : blocked_results) { callbacks.on_tool_result(br); }

        log::info_fmt("agent non-stream step {} completed: tool_calls={}, history_size={}",
                      step + 1, tool_calls.size(), history.size());
    }

    log::warn_fmt("agent: tool call limit reached: max_steps={}", resources_->max_tool_steps());
    co_return llm::ChatResult::ok(container::String("Tool call limit reached"));
}

/// 流式步骤循环
net::Task<llm::ChatResult> Agent::run_session_stream_step(
    net::EventLoop& loop, workspace::Session& session,
    workspace::ConversationHistory& history,
    std::string_view prompt_text,
    const AgentCallbacks& callbacks,
    const net::CancellationToken& cancel) {
    for (int step = 0; step < resources_->max_tool_steps(); ++step) {
        cancel.throw_if_cancelled();
        log::info_fmt("agent stream step {}/{}: sending request", step + 1, resources_->max_tool_steps());
        container::String accumulated_text;
        container::String accumulated_thinking;
        struct PendingToolCall {
            container::String id;
            container::String name;
            container::String arguments;
        };
        std::map<int, PendingToolCall> pending_tools;

        llm::StreamHandlers handlers;
        handlers.on_token = [&callbacks, &accumulated_text, &cancel](std::string_view token) {
            if (cancel.is_cancelled()) {
                throw net::OperationCancelled("request cancelled by user");
            }
            callbacks.on_token(token);
            accumulated_text += token;
        };
        handlers.on_thinking = [&callbacks, &cancel, &accumulated_thinking](std::string_view token) {
            if (cancel.is_cancelled()) {
                throw net::OperationCancelled("request cancelled by user");
            }
            callbacks.on_thinking(token);
            accumulated_thinking += token;
        };
        handlers.on_tool_call = [&](const llm::StreamToolCallDelta& delta) {
            auto& tc = pending_tools[delta.index];
            if (!delta.id.empty()) tc.id = delta.id;
            if (!delta.name.empty()) tc.name = delta.name;
            tc.arguments += delta.arguments;
        };

        auto result = co_await resources_->provider().chat_stream_with_tools_async(
            loop, history, resources_->tools(), {}, handlers);

        callbacks.on_token("");

        callbacks.on_response_stats(result.usage, result.latency);

        if (result.status < 200 || result.status >= 300) {
            if (result.is_context_overflow) {
                log::info_fmt("agent: context_overflow detected (stream), status={}", result.status);
                if (recover_from_overflow(session, loop, resources_->provider(), resources_->tools())) {
                    continue;
                }
                co_return llm::ChatResult::error(400, container::String("上下文超限，压缩恢复失败"));
            }
            log::error_fmt("agent stream failed status={}", result.status);
            co_return llm::ChatResult{.status = result.status,
                                       .raw = container::String(result.raw.data(), result.raw.size())};
        }

        if (pending_tools.empty()) {
            log::info_fmt("agent stream done: no tool calls, text_len={}", accumulated_text.size());

            if (!accumulated_thinking.empty()) {
                session.persist_message(container::String("thinking"),
                                        std::string_view(accumulated_thinking.data(), accumulated_thinking.size()),
                                        resources_->history_db());
            }
            history.add_assistant(std::move(accumulated_text));

            session.persist_message(container::String("user"), prompt_text, resources_->history_db());
            session.persist_message(container::String("assistant"),
                                    std::string_view(history.messages().back().get_all_text()), resources_->history_db());

            session.maybe_compact(loop, resources_->provider(), resources_->tools());

            co_return llm::ChatResult{.status = 200,
                                       .text = container::String(history.messages().back().get_all_text()),
                                       .raw = container::String(result.raw.data(), result.raw.size())};
        }

        // 解析工具调用
        std::vector<llm::ToolCallRequest> tool_calls;
        for (auto& [idx, tc] : pending_tools) {
            llm::ToolCallRequest req;
            req.id = std::move(tc.id);
            req.name = std::move(tc.name);
            std::string err;
            req.arguments = parse_json(std::string(tc.arguments.data(), tc.arguments.size()), err);
            tool_calls.push_back(std::move(req));
        }

        // plan 模式：硬约束过滤非 read_only 工具
        std::vector<llm::ToolCallRequest> blocked_calls;
        std::vector<llm::ToolCallResult> blocked_results;
        if (plan_manager_.in_plan_mode()) {
            auto filter = resources_->tools().filter_plan_mode_tools(tool_calls);
            // 通知 UI 层工具被拦截
            for (const auto& blocked : filter.blocked_calls) {
                auto name_sv = std::string_view(blocked.name.data(), blocked.name.size());
                callbacks.on_tool_blocked(name_sv, "read-only");
            }
            tool_calls = std::move(filter.allowed);
            blocked_calls = std::move(filter.blocked_calls);
            blocked_results = std::move(filter.blocked_results);
        }

        for (const auto& call : tool_calls) {
            log::info_fmt("tool call started: name={}, id={}", call.name, call.id);
        }

        auto tool_results = tool_manager_.execute_tools(tool_calls);

        log::info_fmt("agent stream step {} tool results: {}/{} success",
                      step + 1, tool_results.size(),
                      std::count_if(tool_results.begin(), tool_results.end(),
                                    [](const auto& r) { return r.success; }));

        // 构建 assistant 消息
        {
            container::String assistant_text = std::move(accumulated_text);
            if (!accumulated_thinking.empty()) {
                session.persist_message(container::String("thinking"),
                                        std::string_view(accumulated_thinking.data(), accumulated_thinking.size()),
                                        resources_->history_db());
            }
            auto acp_msg = acp::ACPMessage::assistant_message(assistant_text);
            for (const auto& call : tool_calls) {
                acp_msg.add_tool_use(call);
            }
            // 被拦截的 tool_use 也要加入 assistant 消息，否则 LLM 协议断裂
            for (const auto& bc : blocked_calls) {
                acp_msg.add_tool_use(bc);
            }
            history.add_message(acp_msg);
        }

        for (const auto& tr : tool_results) {
            history.add_tool_result(tr.tool_call_id, tr.name, tr.output);
        }
        // 被拦截的工具也需要加入 history，否则 LLM 协议断裂
        for (const auto& br : blocked_results) {
            history.add_tool_result(br.tool_call_id, br.name, br.output);
        }

        persist_tool_step(session, history, tool_calls, tool_results);

        for (const auto& call : tool_calls) { callbacks.on_tool_call(call); }
        for (const auto& tr : tool_results) { callbacks.on_tool_result(tr); }
        for (const auto& br : blocked_results) { callbacks.on_tool_result(br); }
    }

    log::warn_fmt("agent: tool call limit reached: max_steps={}", resources_->max_tool_steps());
    co_return llm::ChatResult::ok(container::String("Tool call limit reached"));
}

/// 持久化工具步骤
void Agent::persist_tool_step(workspace::Session& session,
                              workspace::ConversationHistory& history,
                              const std::vector<llm::ToolCallRequest>& calls,
                              const std::vector<llm::ToolCallResult>& results) {
    auto& last_msg = history.messages()[history.size() - 1];
    auto text = last_msg.get_all_text();
    session.persist_assistant_with_tools(text, calls, resources_->history_db());
    for (const auto& r : results) {
        session.persist_tool_result(r.tool_call_id, r.name, r.output, resources_->history_db());
    }
}

} // namespace ben_gear::agent
