#include "ben_gear/agent/agent_impl.hpp"

#include <map>
#include <vector>

namespace ben_gear::agent {

net::Task<llm::ChatResult> Agent::run_session_async(net::EventLoop& loop,
                                                     workspace::Session& session,
                                                     base::container::String prompt,
                                                     const AgentCallbacks& callbacks,
                                                     const net::CancellationToken& cancel) {
    // 输入验证：只检查空，长度限制由 LLM API 自己处理
    if (prompt.empty()) {
        log::error_fmt("agent: invalid prompt (empty)");
        co_return llm::ChatResult{400, {}, {}, container::String("Invalid input: prompt is empty")};
    }
    
    auto& history = session.history();

    log::info_fmt("agent session started: session_id={}, stream={}, prompt_len={}",
        std::string_view(session.session_id().data(), session.session_id().size()),
        resources_->settings().stream, prompt.size());
    if (history.empty()) {
        history.add_system(std::string_view(AgentImpl::build_system_prompt(*resources_)));
    }
    
    // 添加用户消息到 history（使用 string_view，零拷贝）
    history.add_user(std::string_view(prompt.data(), prompt.size()));

    if (resources_->settings().stream) {
        // 流式路径：直接使用 prompt，零拷贝
        co_return co_await run_session_stream_step(loop, session, history, 
            std::string_view(prompt.data(), prompt.size()), callbacks, cancel);
    }

    // 非流式路径：需要拷贝 prompt 用于后续持久化
    auto prompt_copy = std::string(prompt.data(), prompt.size());

    // 非流式路径（重试已在 with_http_retry_async 内部处理）
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
            // HTTP 错误或无效响应，提取错误信息
            std::string error_msg;
            if (response.contains("error") && response["error"].is_object()) {
                error_msg = response["error"].value("message", "");
            }
            if (error_msg.empty()) {
                error_msg = response.dump();
            }
            int status = 0;
            if (response.contains("error") && response["error"].contains("status")) {
                Json err = response["error"];
                status = err["status"].get<int>();
            }
            log::error_fmt("agent non-stream invalid response: status={}", status);
            co_return llm::ChatResult{status > 0 ? status : 500, {}, std::move(error_msg), {}};
        }

        if (!llm::ToolCallManager::has_tool_calls(response, resources_->settings().provider)) {
            log::debug_fmt("agent: no tool calls, extracting text");
            AgentImpl::emit_thinking(response, callbacks, resources_->settings().provider);
            auto text = AgentImpl::extract_response_text(response, resources_->settings().provider);
            log::info_fmt("agent: extracted text, size={}", text.size());
            history.add_assistant(std::string_view(text));
            callbacks.on_token(text);

            session.persist_message(container::String("user"), std::string_view(prompt_copy), resources_->history_db());
            session.persist_message(container::String("assistant"), std::string_view(text), resources_->history_db());

            session.maybe_compact(loop, resources_->provider(), resources_->tools());

            co_return llm::ChatResult{200, text, response.dump(), {}};
        }

        auto tool_calls = AgentImpl::extract_tool_calls(response, tool_manager_, resources_->settings().provider);
        for (const auto& call : tool_calls) {
            log::info_fmt("tool call started: name={}, id={}, args={}", call.name, call.id, call.arguments.dump());
        }

        // 设置工作流命名空间（username::workspace::session_id），工具自动读取
        // 使用 RAII 守卫，确保异常时也能清理命名空间
        std::string ns;
        ns.reserve(resources_->workspace_context().username.size() + 
                   resources_->workspace_context().workspace_name.size() + 
                   session.session_id().size() + 4);  // +4 for "::"
        ns.append(resources_->workspace_context().username.data(), resources_->workspace_context().username.size());
        ns += "::";
        ns.append(resources_->workspace_context().workspace_name.data(), resources_->workspace_context().workspace_name.size());
        ns += "::";
        ns.append(session.session_id().data(), session.session_id().size());
       
        workflow::WorkflowEngine::NamespaceGuard ns_guard(ns);
        auto results = tool_manager_.execute_tools(tool_calls);
        for (const auto& result : results) {
            log::info_fmt("tool call completed: name={}, success={}, output_size={}",
                          result.name, result.success, result.output.size());
        }

        AgentImpl::add_assistant_message_with_tools_to(response, tool_calls, history, resources_->settings().provider);
        
        // 添加 tool_result 到 history（关键！LLM 需要看到工具返回结果）
        for (const auto& result : results) {
            history.add_tool_result(result.tool_call_id, result.name, result.output);
        }
        
        persist_tool_step(session, history, tool_calls, results);

       // 先发出 thinking，再发出工具调用/结果（与流式路径顺序一致）
       AgentImpl::emit_thinking(response, callbacks, resources_->settings().provider);

       for (const auto& call : tool_calls) { callbacks.on_tool_call(call); }
         for (const auto& result : results) { callbacks.on_tool_result(result); }
        
        log::info_fmt("agent non-stream step {} completed: tool_calls={}, history_size={}", 
                      step + 1, tool_calls.size(), history.size());
    }

    log::warn_fmt("agent: tool call limit reached: max_steps={}", resources_->max_tool_steps());
    co_return llm::ChatResult{200, "Tool call limit reached", "", {}};
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
        struct PendingToolCall {
            container::String id;
            container::String name;
            container::String arguments;
        };
        std::map<int, PendingToolCall> pending_tools;

        llm::StreamHandlers handlers;
        handlers.on_token = [&callbacks, &accumulated_text, &cancel](std::string_view token) {
            // 在每次收到 token 时检查取消状态
            if (cancel.is_cancelled()) {
                throw net::OperationCancelled("request cancelled by user");
            }
            callbacks.on_token(token);
            accumulated_text += token;
        };
        handlers.on_thinking = [&callbacks, &cancel](std::string_view token) {
            // 在每次收到 thinking 时检查取消状态
            if (cancel.is_cancelled()) {
                throw net::OperationCancelled("request cancelled by user");
            }
            callbacks.on_thinking(token);
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

        // 检查流式请求状态
        if (result.status < 200 || result.status >= 300) {
            log::error_fmt("agent stream failed status={}", result.status);
            co_return llm::ChatResult{result.status, {},
                std::string(result.raw.data(), result.raw.size()), {}};
        }

        if (pending_tools.empty()) {
            log::info_fmt("agent stream done: no tool calls, text_len={}", accumulated_text.size());
            history.add_assistant(std::move(accumulated_text));

            session.persist_message(container::String("user"), prompt_text, resources_->history_db());
            session.persist_message(container::String("assistant"), 
                std::string_view(history.messages().back().get_all_text()), resources_->history_db());

            session.maybe_compact(loop, resources_->provider(), resources_->tools());

            co_return llm::ChatResult{200, std::string(history.messages().back().get_all_text()), 
                std::string(result.raw.data(), result.raw.size()), {}};
        }

        // 解析工具调用
        std::vector<llm::ToolCallRequest> tool_calls;
        for (auto& [idx, tc] : pending_tools) {
            llm::ToolCallRequest req;
            req.id = std::move(tc.id);
            req.name = std::move(tc.name);
            std::string err;
            req.arguments = parse_json(std::string(tc.arguments.data(), tc.arguments.size()), err);
            if (!err.empty()) {
                log::error_fmt("stream tool call arguments parse failed: name={} error={}", tc.name, err);
                req.arguments = Json::object({{"_parse_error", err}, {"_raw_arguments", std::string(tc.arguments.data(), tc.arguments.size())}});
            }
            tool_calls.push_back(std::move(req));
        }

        // 设置工作流命名空间（username::workspace::session_id），工具自动读取
        // 使用 RAII 守卫，确保异常时也能清理命名空间
        std::string ns;
        ns.reserve(resources_->workspace_context().username.size() + 
                   resources_->workspace_context().workspace_name.size() + 
                   session.session_id().size() + 4);  // +4 for "::"
        ns.append(resources_->workspace_context().username.data(), resources_->workspace_context().username.size());
        ns += "::";
        ns.append(resources_->workspace_context().workspace_name.data(), resources_->workspace_context().workspace_name.size());
        ns += "::";
        ns.append(session.session_id().data(), session.session_id().size());
        workflow::WorkflowEngine::NamespaceGuard ns_guard(ns);
        auto tool_results = tool_manager_.execute_tools(tool_calls);

        log::info_fmt("agent stream step {}: {} tool calls executed, {} success",
                      step + 1, tool_results.size(),
                      std::count_if(tool_results.begin(), tool_results.end(),
                                   [](const auto& r) { return r.success; }));

        // 更新 history（创建 assistant 消息）
        auto assistant_msg = acp::ACPMessage::assistant_message(
            base::container::String(accumulated_text.data(), accumulated_text.size()));
        
        // 添加工具调用
        for (const auto& call : tool_calls) {
            assistant_msg.add_tool_use(call);
        }
        
        history.add_message(assistant_msg);

        for (const auto& tr : tool_results) {
            history.add_tool_result(tr.tool_call_id, tr.name, tr.output);
        }

        // 持久化
        auto text = assistant_msg.get_all_text();
        session.persist_assistant_with_tools(text, tool_calls, resources_->history_db());
        for (const auto& tr : tool_results) {
            session.persist_tool_result(tr.tool_call_id, tr.name, tr.output, resources_->history_db());
        }

        for (const auto& call : tool_calls) { callbacks.on_tool_call(call); }
        for (const auto& tr : tool_results) { callbacks.on_tool_result(tr); }
    }

    co_return llm::ChatResult{200, "Tool call limit reached", "", {}};
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

}  // namespace ben_gear::agent
