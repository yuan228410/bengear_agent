#include "agent/runtime/runtime.hpp"
#include "agent/core/interface/event_sink.hpp"
#include "base/log/logger.hpp"
#include "llm/provider_error.hpp"
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
            req.id = container::String(Json(tc["id"]).get<std::string>().c_str());
            req.name = container::String(Json(tc["function"]["name"]).get<std::string>().c_str());
            std::string args_str = Json(tc["function"]["arguments"]).get<std::string>();
            try { req.arguments = Json::parse(args_str); } catch (...) { req.arguments = Json::object(); }
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
            req.id = container::String(Json(block["id"]).get<std::string>().c_str());
            req.name = container::String(Json(block["name"]).get<std::string>().c_str());
            req.arguments = block.value("input", Json::object());
            calls.push_back(std::move(req));
        }
    }
    return calls;
}

/// 从 JSON 响应中提取文本
std::string extract_text(Json& response, config::Provider provider) {
    if (provider == config::Provider::openai) {
        if (response.contains("choices") && response["choices"].is_array() && !response["choices"].empty()) {
            auto msg = response["choices"][0]["message"];  // ProxyRef (non-const)
            if (msg.contains("content") && !msg["content"].is_null())
                return Json(msg["content"]).get<std::string>();
        }
    } else {
        if (response.contains("content") && response["content"].is_array()) {
            auto content = response["content"];
            std::string text;
            for (size_t i = 0; i < content.size(); ++i) {
                auto block = content[i];
                if (block.contains("type") && Json(block["type"]).get<std::string>() == "text")
                    text += Json(block["text"]).get<std::string>();
            }
            return text;
        }
    }
    return "";
}

/// 从 JSON 响应中提取思考内容
std::string extract_thinking(Json& response, config::Provider provider) {
    if (provider == config::Provider::openai) {
        if (response.contains("choices") && response["choices"].is_array() && !response["choices"].empty()) {
            auto msg = response["choices"][0]["message"];
            if (msg.contains("reasoning_content") && !msg["reasoning_content"].is_null())
                return Json(msg["reasoning_content"]).get<std::string>();
        }
    } else {
        if (response.contains("content") && response["content"].is_array()) {
            auto content = response["content"];
            for (size_t i = 0; i < content.size(); ++i) {
                auto block = content[i];
                if (Json(block["type"]).get<std::string>() == "thinking" && block.contains("thinking"))
                    return Json(block["thinking"]).get<std::string>();
            }
        }
    }
    return "";
}

} // namespace

net::Task<llm::ChatResult> Runtime::run_session_async(
    net::EventLoop& loop, workspace::Session& session,
    container::String prompt, const AgentEventSink& event_sink,
    const net::CancellationToken& cancel, const llm::ToolRegistry* tool_override) {

    const llm::ToolRegistry& tool_reg = tool_override ? *tool_override : tools_;
    auto& history = session.history();

    // 构建系统提示
    std::string sys_prompt;
    auto sp = settings_.agent.system_prompt;
    if (!sp.empty()) {
        sys_prompt.append(sp.data(), sp.size());
    } else {
        sys_prompt = "You are BenGear, an AI coding agent for software engineering tasks.\n";
    }
    history.set_system_prompt(sys_prompt);
    history.add_user(std::string_view(prompt.data(), prompt.size()));

    const int max_steps = max_tool_steps_ > 0 ? max_tool_steps_ : 20;
    const int max_calls = max_tool_calls_ > 0 ? max_tool_calls_ : 50;
    int total_calls = 0;

    for (int step = 0; step < max_steps; ++step) {
        cancel.throw_if_cancelled();

        auto response = co_await provider_.chat_with_tools_async(
            loop, history, tool_reg, {}, cancel, {});

        // 检查错误
        bool has_content = false;
        if (response.contains("choices") && response["choices"].is_array() && !response["choices"].empty())
            has_content = true;
        else if (response.contains("content") && response["content"].is_array())
            has_content = true;

        if (!has_content) {
            std::string error_msg;
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

        ToolCallManager tool_mgr(tool_reg, core_pool_,
                                       std::chrono::seconds(30),
                                       shared_from_this());
        std::vector<llm::ToolCallResult> results;
        for (const auto& c : tool_calls)
            results.push_back(tool_mgr.execute_tool(c));

        for (const auto& r : results) event_sink.on_tool_result(r);

        // 添加到历史
        auto asst_text = extract_text(response, settings_.provider);
        auto acp_msg = acp::ACPMessage::assistant_message(
            container::String(asst_text.data(), asst_text.size()));
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

