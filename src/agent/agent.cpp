#include "ben_gear/agent/agent_impl.hpp"
#include "ben_gear/llm/provider_error.hpp"

#include <algorithm>
#include <map>
#include <vector>
#include <string>
#include "ben_gear/base/log/logger.hpp"

namespace ben_gear::agent {

static container::String make_session_title(std::string_view prompt) {
    std::string title;
    title.reserve(48);
    bool prev_space = true;
    for (char ch : prompt) {
        unsigned char c = static_cast<unsigned char>(ch);
        if (ch == '\n' || ch == '\r' || ch == '\t' || ch == ' ') {
            if (!prev_space && title.size() < 48) title.push_back(' ');
            prev_space = true;
            continue;
        }
        if (c < 0x20) continue;
        title.push_back(ch);
        prev_space = false;
        if (title.size() >= 48) break;
    }
    while (!title.empty() && title.back() == ' ') title.pop_back();
    if (title.empty()) title = "New Session";
    if (prompt.size() > title.size()) title += "…";
    return container::String(title.c_str());
}

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

static int normalize_limit(int value) noexcept {
    return value > 0 ? value : 1;
}

static bool is_update_todo_call(const llm::ToolCallRequest& call) {
    return std::string_view(call.name.data(), call.name.size()) == "update_todo";
}

static bool is_update_todo_result(const llm::ToolCallResult& result) {
    return std::string_view(result.name.data(), result.name.size()) == "update_todo";
}

static void notify_visible_tool_calls(const std::vector<llm::ToolCallRequest>& calls,
                                      const AgentCallbacks& callbacks) {
    for (const auto& call : calls) {
        if (!is_update_todo_call(call)) callbacks.on_tool_call(call);
    }
}

static void notify_visible_tool_results(const std::vector<llm::ToolCallResult>& results,
                                        const AgentCallbacks& callbacks) {
    for (const auto& result : results) {
        if (!is_update_todo_result(result)) callbacks.on_tool_result(result);
    }
}

static container::String json_string_field(const Json& json, const char* name) {
    if (!json.is_object() || !json.contains(name) || json[name].is_null()) return {};
    if (json[name].is_string()) return container::String(json[name].get<std::string>());
    return container::String(json[name].dump());
}

static int json_int_field(const Json& json, const char* name, int fallback = 0) {
    if (!json.is_object() || !json.contains(name) || json[name].is_null()) return fallback;
    if (json[name].is_number_integer()) return json[name].get<int>();
    if (json[name].is_number()) return static_cast<int>(json[name].get<double>());
    if (json[name].is_string()) {
        try { return std::stoi(json[name].get<std::string>()); } catch (...) { return fallback; }
    }
    return fallback;
}

static orchestration::TodoItem todo_item_from_tool_json(const Json& json, int order) {
    orchestration::TodoItem item;
    item.todo_id = json_string_field(json, "id");
    if (item.todo_id.empty()) item.todo_id = json_string_field(json, "todo_id");
    item.title = json_string_field(json, "title");
    if (item.todo_id.empty()) item.todo_id = item.title;
    item.active_form = json_string_field(json, "active_form");
    item.result_summary = json_string_field(json, "result_summary");
    auto status = json_string_field(json, "status");
    item.has_status = !status.empty();
    item.status = item.has_status
        ? orchestration::todo_status_from_string(std::string_view(status.data(), status.size()))
        : orchestration::TodoStatus::pending;
    item.has_progress = json.contains("progress");
    item.progress = json_int_field(json, "progress", 0);
    item.order = json_int_field(json, "order", order);
    return item;
}

static llm::ToolCallResult handle_update_todo_call(const llm::ToolCallRequest& call,
                                                   const AgentCallbacks& callbacks) {
    llm::ToolCallResult result;
    result.tool_call_id = call.id;
    result.name = call.name;
    auto action = json_string_field(call.arguments, "action");
    std::string action_str(action.data(), action.size());
    if (action_str.empty()) action_str = "update_item";
    int updated = 0;
    if (action_str == "clear") {
        callbacks.on_todo_update(orchestration::TodoItem{}, "clear");
        result.success = true;
        result.output = container::String(R"({"ok":true,"updated":0})");
        return result;
    }
    if (action_str == "set_items") {
        callbacks.on_todo_update(orchestration::TodoItem{}, "clear");
        const auto items = call.arguments.contains("items") ? call.arguments["items"] : Json::array();
        if (items.is_array()) {
            for (size_t i = 0; i < items.size(); ++i) {
                auto item = todo_item_from_tool_json(items[i], static_cast<int>(i) + 1);
                if (!item.title.empty() || !item.todo_id.empty()) {
                    callbacks.on_todo_update(item, "set_items");
                    ++updated;
                }
            }
        }
    } else {
        const auto item_json = call.arguments.contains("item") ? call.arguments["item"] : call.arguments;
        auto item = todo_item_from_tool_json(item_json, 1);
        if (!item.title.empty() || !item.todo_id.empty()) {
            callbacks.on_todo_update(item, "update_item");
            updated = 1;
        }
    }
    result.success = true;
    result.output = container::String(std::string(R"({"ok":true,"updated":)") + std::to_string(updated) + "}");
    return result;
}

static std::vector<llm::ToolCallResult> execute_tool_calls(const std::vector<llm::ToolCallRequest>& calls,
                                                           const llm::ToolCallManager& tool_manager,
                                                           const AgentCallbacks& callbacks) {
    std::vector<llm::ToolCallResult> results;
    results.reserve(calls.size());
    for (const auto& call : calls) {
        if (is_update_todo_call(call)) {
            results.push_back(handle_update_todo_call(call, callbacks));
        } else {
            results.push_back(tool_manager.execute_tool(call));
        }
    }
    return results;
}

static llm::ChatResult make_tool_limit_result(int max_steps,
                                              int steps_used,
                                              int max_tool_calls,
                                              int tool_calls_used,
                                              int max_tool_calls_per_step,
                                              int tool_calls_in_step,
                                              std::string_view message) {
    return llm::ChatResult::tool_limit(
        max_steps,
        steps_used,
        max_tool_calls,
        tool_calls_used,
        max_tool_calls_per_step,
        tool_calls_in_step,
        container::String(message.data(), message.size()));
}

net::Task<llm::ChatResult> Agent::run_session_async(net::EventLoop& loop,
                                                     workspace::Session& session,
                                                     base::container::String prompt,
                                                     const AgentCallbacks& callbacks,
                                                     const net::CancellationToken& cancel,
                                                     const llm::ToolRegistry* tool_override) {
    co_return co_await run_session_async(loop, session, std::move(prompt), callbacks,
                                         RunOptions{}, cancel, tool_override);
}

net::Task<llm::ChatResult> Agent::run_session_async(net::EventLoop& loop,
                                                     workspace::Session& session,
                                                     base::container::String prompt,
                                                     const AgentCallbacks& callbacks,
                                                     RunOptions options,
                                                     const net::CancellationToken& cancel,
                                                     const llm::ToolRegistry* tool_override) {
    // 工具注册表：子 Agent 可传入过滤后的注册表
    const llm::ToolRegistry& tool_registry = tool_override ? *tool_override : resources_->tools();

    if (prompt.empty()) {
        log::error_fmt("agent: invalid prompt (empty)");
        co_return llm::ChatResult::invalid_input(container::String("Invalid input: prompt is empty"));
    }

    auto& history = session.history();

    log::info_fmt("agent session started: session_id={}, stream={}, prompt_len={}",
                  std::string_view(session.session_id().data(), session.session_id().size()),
                  resources_->settings().stream, prompt.size());
    auto system_prompt = AgentImpl::build_system_prompt(*resources_);
    if (!options.system_prompt.empty()) {
        system_prompt += "\n";
        system_prompt.append(options.system_prompt.data(), options.system_prompt.size());
    }
    const bool plan_mode = plan_manager_.in_plan_mode();
    const bool has_todo_discipline = system_prompt.find("Execution mode:") != std::string::npos &&
        system_prompt.find("update_todo") != std::string::npos;
    const bool system_refreshed = history.set_system_prompt(system_prompt);
    log::info_fmt("agent system prompt: session_id={} refreshed={} len={} todo_discipline={} mode={} override={}",
                  std::string_view(session.session_id().data(), session.session_id().size()),
                  system_refreshed, system_prompt.size(), has_todo_discipline,
                  plan_mode ? "plan" : "execution", !options.system_prompt.empty());

    // 添加用户消息到 history，并在 Agent 边界统一持久化一次。
    // Server / stream / non-stream / tool-loop 都不再重复写 user，避免历史展示重复。
    const bool first_user_message = history.size() == 1;
    container::String model_prompt;
    if (plan_mode) {
        model_prompt = container::String("[Mode: plan]\n");
    } else {
        model_prompt = container::String("[Mode: execution]\n");
    }
    model_prompt.append(prompt);
    auto todo_context = callbacks.todo_context_summary();
    if (!todo_context.empty()) {
        model_prompt.append(todo_context);
    }
    const auto prompt_view = std::string_view(model_prompt.data(), model_prompt.size());
    history.add_user(prompt_view);
    session.persist_message(container::String("user"), std::string_view(prompt.data(), prompt.size()), resources_->history_db());
    if (first_user_message) {
        resources_->history_db().rename_session(
            session.workspace_context().workspace_name,
            session.session_id(),
            make_session_title(prompt_view));
    }

    if (resources_->settings().stream) {
        co_return co_await run_session_stream_step(loop, session, history,
                                                    callbacks, cancel, tool_override, options);
    }

    // 非流式路径
    const int max_tool_steps = normalize_limit(options.max_tool_steps > 0
        ? options.max_tool_steps
        : resources_->max_tool_steps());
    const int max_tool_calls = normalize_limit(resources_->max_tool_calls());
    const int max_tool_calls_per_step = normalize_limit(resources_->max_tool_calls_per_step());
    int total_tool_calls = 0;
    for (int step = 0; step < max_tool_steps; ++step) {
        cancel.throw_if_cancelled();
        log::info_fmt("agent non-stream step {}/{}: sending request", step + 1, max_tool_steps);

        auto response = co_await resources_->provider().chat_with_tools_async(
            loop, history, tool_registry, {}, cancel, options.model_override);

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
                co_return llm::ChatResult::context_overflow(container::String("上下文超限，压缩恢复失败"));
            }

            log::error_fmt("agent non-stream invalid response: status={}", status);
            co_return llm::ChatResult::error(status > 0 ? status : 500,
                                             container::String(error_msg.c_str()));
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
            callbacks.on_response_stats(tracker.last_usage(), tracker.last_latency(),
                                        std::string_view(resources_->settings().model.data(),
                                                         resources_->settings().model.size()),
                                        resources_->settings().context_length);

            session.persist_message(container::String("assistant"), std::string_view(text), resources_->history_db());

            session.maybe_compact(loop, resources_->provider(), resources_->tools());

            co_return llm::ChatResult::ok(std::move(text), container::String(response.dump()));
        }

        auto tool_calls = AgentImpl::extract_tool_calls(response, tool_manager_, resources_->settings().provider);
        if (static_cast<int>(tool_calls.size()) > max_tool_calls_per_step) {
            log::warn_fmt("agent: per-step tool call limit reached: step={} calls={} max_per_step={} total_used={} max_total={}",
                          step + 1, tool_calls.size(), max_tool_calls_per_step,
                          total_tool_calls, max_tool_calls);
            co_return make_tool_limit_result(max_tool_steps, step + 1,
                                             max_tool_calls, total_tool_calls,
                                             max_tool_calls_per_step, static_cast<int>(tool_calls.size()),
                                             "Per-step tool call limit reached");
        }
        if (total_tool_calls + static_cast<int>(tool_calls.size()) > max_tool_calls) {
            log::warn_fmt("agent: total tool call limit reached: step={} calls={} total_used={} max_total={}",
                          step + 1, tool_calls.size(), total_tool_calls, max_tool_calls);
            co_return make_tool_limit_result(max_tool_steps, step + 1,
                                             max_tool_calls, total_tool_calls,
                                             max_tool_calls_per_step, static_cast<int>(tool_calls.size()),
                                             "Total tool call limit reached");
        }
        total_tool_calls += static_cast<int>(tool_calls.size());

        // plan 模式：硬约束过滤非 read_only 工具
        std::vector<llm::ToolCallRequest> blocked_calls;
        std::vector<llm::ToolCallResult> blocked_results;
        if (plan_manager_.in_plan_mode()) {
            auto filter = tool_registry.filter_plan_mode_tools(tool_calls);
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

        // 先通知 UI 工具调用开始，再执行（确保子 Agent 事件排在 delegate_task 工具框之后）
        notify_visible_tool_calls(tool_calls, callbacks);

        auto results = execute_tool_calls(tool_calls, tool_manager_, callbacks);

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

        // add_tool_result 前保存 assistant 文本（之后 last_msg 会变）
        auto& asst_msg = history.messages()[history.size() - 1];
        auto asst_text = asst_msg.get_all_text();

        for (const auto& result : results) {
            history.add_tool_result(result.tool_call_id, result.name, result.output);
        }
        for (const auto& br : blocked_results) {
            history.add_tool_result(br.tool_call_id, br.name, br.output);
        }

        persist_tool_step(session, history, tool_calls, results,
                          container::String(asst_text.data(), asst_text.size()));

        AgentImpl::emit_thinking(response, callbacks, resources_->settings().provider);

        notify_visible_tool_results(results, callbacks);
        notify_visible_tool_results(blocked_results, callbacks);

        log::info_fmt("agent non-stream step {} completed: tool_calls={}, history_size={}",
                      step + 1, tool_calls.size(), history.size());
    }

    log::warn_fmt("agent: tool step limit reached: max_steps={} total_tool_calls={}",
                  max_tool_steps, total_tool_calls);
    co_return make_tool_limit_result(max_tool_steps, max_tool_steps,
                                     max_tool_calls, total_tool_calls,
                                     max_tool_calls_per_step, 0,
                                     "Tool step limit reached");
}

/// 流式步骤循环
net::Task<llm::ChatResult> Agent::run_session_stream_step(
    net::EventLoop& loop, workspace::Session& session,
    workspace::ConversationHistory& history,
    const AgentCallbacks& callbacks,
    const net::CancellationToken& cancel,
    const llm::ToolRegistry* tool_override,
    const RunOptions& options) {
    // 工具注册表：子 Agent 可传入过滤后的注册表
    const llm::ToolRegistry& tool_registry = tool_override ? *tool_override : resources_->tools();
    const int max_tool_steps = normalize_limit(options.max_tool_steps > 0
        ? options.max_tool_steps
        : resources_->max_tool_steps());
    const int max_tool_calls = normalize_limit(resources_->max_tool_calls());
    const int max_tool_calls_per_step = normalize_limit(resources_->max_tool_calls_per_step());
    int total_tool_calls = 0;
    for (int step = 0; step < max_tool_steps; ++step) {
        cancel.throw_if_cancelled();
        log::info_fmt("agent stream step {}/{}: sending request", step + 1, max_tool_steps);

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
            loop, history, tool_registry, {}, handlers, cancel, options.model_override);

        callbacks.on_token("");


        if (result.status < 200 || result.status >= 300) {
            if (result.is_context_overflow) {
                log::info_fmt("agent: context_overflow detected (stream), status={}", result.status);
                if (recover_from_overflow(session, loop, resources_->provider(), resources_->tools())) {
                    continue;
                }
                co_return llm::ChatResult::context_overflow(container::String("上下文超限，压缩恢复失败"));
            }
            log::error_fmt("agent stream failed status={}", result.status);
            auto message = result.raw.empty()
                ? container::String("Provider stream request failed")
                : container::String(result.raw.data(), result.raw.size());
            auto chat_result = llm::ChatResult::error(result.status, message);
            chat_result.raw = container::String(result.raw.data(), result.raw.size());
            chat_result.usage = result.usage;
            chat_result.latency = result.latency;
            co_return chat_result;
        }

        if (pending_tools.empty()) {
            log::info_fmt("agent stream done: no tool calls, text_len={}", accumulated_text.size());

            // 只在最终正文步骤显示统计信息（工具调用中间步骤不显示）
            callbacks.on_response_stats(result.usage, result.latency,
                                        std::string_view(resources_->settings().model.data(),
                                                         resources_->settings().model.size()),
                                                         resources_->settings().context_length);

            if (!accumulated_thinking.empty()) {
                session.persist_message(container::String("thinking"),
                                        std::string_view(accumulated_thinking.data(), accumulated_thinking.size()),
                                        resources_->history_db());
            }
            history.add_assistant(std::move(accumulated_text));

            session.persist_message(container::String("assistant"),
                                    std::string_view(history.messages().back().get_all_text()), resources_->history_db());

            session.maybe_compact(loop, resources_->provider(), resources_->tools());

            co_return llm::ChatResult{.status = 200,
                                       .text = container::String(history.messages().back().get_all_text()),
                                       .raw = container::String(result.raw.data(), result.raw.size()),
                                       .error_message = {},
                                       .usage = {},
                                       .latency = {}};
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
        if (static_cast<int>(tool_calls.size()) > max_tool_calls_per_step) {
            log::warn_fmt("agent: per-step tool call limit reached (stream): step={} calls={} max_per_step={} total_used={} max_total={}",
                          step + 1, tool_calls.size(), max_tool_calls_per_step,
                          total_tool_calls, max_tool_calls);
            co_return make_tool_limit_result(max_tool_steps, step + 1,
                                             max_tool_calls, total_tool_calls,
                                             max_tool_calls_per_step, static_cast<int>(tool_calls.size()),
                                             "Per-step tool call limit reached");
        }
        if (total_tool_calls + static_cast<int>(tool_calls.size()) > max_tool_calls) {
            log::warn_fmt("agent: total tool call limit reached (stream): step={} calls={} total_used={} max_total={}",
                          step + 1, tool_calls.size(), total_tool_calls, max_tool_calls);
            co_return make_tool_limit_result(max_tool_steps, step + 1,
                                             max_tool_calls, total_tool_calls,
                                             max_tool_calls_per_step, static_cast<int>(tool_calls.size()),
                                             "Total tool call limit reached");
        }
        total_tool_calls += static_cast<int>(tool_calls.size());

        // plan 模式：硬约束过滤非 read_only 工具
        std::vector<llm::ToolCallRequest> blocked_calls;
        std::vector<llm::ToolCallResult> blocked_results;
        if (plan_manager_.in_plan_mode()) {
            auto filter = tool_registry.filter_plan_mode_tools(tool_calls);
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

        // 先通知 UI 工具调用开始，再执行（确保子 Agent 事件排在 delegate_task 工具框之后）
        notify_visible_tool_calls(tool_calls, callbacks);

        auto tool_results = execute_tool_calls(tool_calls, tool_manager_, callbacks);

        log::info_fmt("agent stream step {} tool results: {}/{} success",
                      step + 1, tool_results.size(),
                      std::count_if(tool_results.begin(), tool_results.end(),
                                    [](const auto& r) { return r.success; }));

        // 构建 assistant 消息
        container::String assistant_text_for_persist;
        {
            container::String assistant_text = std::move(accumulated_text);
            assistant_text_for_persist = assistant_text;
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

        persist_tool_step(session, history, tool_calls, tool_results, assistant_text_for_persist);

        notify_visible_tool_results(tool_results, callbacks);
        notify_visible_tool_results(blocked_results, callbacks);
    }

    log::warn_fmt("agent: tool step limit reached (stream): max_steps={} total_tool_calls={}",
                  max_tool_steps, total_tool_calls);
    co_return make_tool_limit_result(max_tool_steps, max_tool_steps,
                                     max_tool_calls, total_tool_calls,
                                     max_tool_calls_per_step, 0,
                                     "Tool step limit reached");
}

/// 持久化工具步骤
void Agent::persist_tool_step(workspace::Session& session,
                              workspace::ConversationHistory&,
                              const std::vector<llm::ToolCallRequest>& calls,
                              const std::vector<llm::ToolCallResult>& results,
                              const container::String& assistant_text) {
    session.persist_assistant_with_tools(assistant_text, calls, resources_->history_db());
    for (const auto& r : results) {
        session.persist_tool_result(r.tool_call_id, r.name, r.output, resources_->history_db());
    }
}

} // namespace ben_gear::agent
