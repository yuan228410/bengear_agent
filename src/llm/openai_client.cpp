#include "llm/openai_client.hpp"
#include "log/logger.hpp"
#include "llm/usage_helpers.hpp"
#include "llm/retry.hpp"

namespace ben_gear::llm {

OpenAiClient::OpenAiClient(config::Settings settings, std::shared_ptr<net::HttpClient> http)
    : settings_(std::move(settings)),
      http_(std::move(http)),
      endpoint_url_(llm::endpoint_url(settings_, "/v1/chat/completions")) {}

// ─── 核心方法 ────────────────────────────────────────────────────

net::Task<StreamResult> OpenAiClient::chat_stream(
    net::EventLoop& loop, const ConversationHistory& history, const capabilities::tool::ToolRegistry& tools,
    const capabilities::tool::ToolChoiceConfig& tool_choice, StreamHandlers handlers,
    const net::CancellationToken& cancel) {
    ensure_api_key();
    auto body = build_body_with_tools(history, tools, tool_choice, true);
    auto headers = build_headers();
    auto usage_ptr = handlers.usage_out;
    OpenAiStreamParser parser(std::move(handlers));
    auto resp = co_await http_->post_json_stream_async(loop, endpoint_url_, body, headers,
        [&](std::string_view chunk) {
            if (cancel.is_cancelled()) throw net::OperationCancelled("cancelled");
            if (!parser.stopped()) parser.parse(chunk);
            return !parser.stopped();
        });
    parser.finish();
    StreamResult r; r.status = resp.status; r.raw = resp.body;
    if (usage_ptr) r.usage = *usage_ptr;
    co_return r;
}

net::Task<Json> OpenAiClient::chat(net::EventLoop& loop,
                                    const ConversationHistory& history,
                                    const capabilities::tool::ToolRegistry& tools,
                                    const capabilities::tool::ToolChoiceConfig& tool_choice,
                                    const net::CancellationToken& cancel) {
    ensure_api_key();
    auto body = build_body_with_tools(history, tools, tool_choice, false);
    auto headers = build_headers();
    co_return co_await with_http_retry_async(loop, settings_, "openai chat",
        [&]() { return http_->post_json_async(loop, endpoint_url_, body, headers); },
        [](net::HttpResponse&& resp) -> Json {
            std::string error;
            auto result = parse_json(resp.body, error);
            if (!error.empty()) {
                log::error_fmt("openai parse failed: status={} error={}", resp.status, error);
                throw ProviderError(ProviderErrorKind::unknown, resp.status, "openai json parse: " + error);
            }
            return result;
        }, cancel);
}

/// 解析 OpenAI 非流式响应 JSON，通过 StreamHandlers 回调发出
/// OpenAI 格式: { choices: [{ message: { content, tool_calls, reasoning_content } }], usage: {...} }
void OpenAiClient::emit_non_stream_result(const Json& response, StreamHandlers& handlers) {
    const auto& choices = response["choices"];
    if (!choices.is_array() || choices.empty()) return;

    const auto& choice0 = choices[0];
    auto msg_it = choice0.find("message");
    if (msg_it == choice0.end() || !msg_it->is_object()) return;
    const auto& msg = *msg_it;

    // 文本内容
    const auto& content_field = msg["content"];
    if (content_field.is_string()) {
        std::string text = content_field.get<std::string>();
        if (!text.empty() && handlers.on_token) handlers.on_token(text);
    }
    // 推理内容（部分模型支持）
    const auto& reasoning_field = msg["reasoning_content"];
    if (reasoning_field.is_string()) {
        std::string reasoning = reasoning_field.get<std::string>();
        if (!reasoning.empty() && handlers.on_thinking) handlers.on_thinking(reasoning);
    }
    // 工具调用
    const auto& tool_calls = msg["tool_calls"];
    if (tool_calls.is_array()) {
        for (const auto& tc : tool_calls) {
            llm::StreamToolCallDelta delta;
            delta.index = tc.value("index", 0);
            delta.id = tc.value("id", std::string{});
            const auto& fn = tc["function"];
            if (fn.is_object()) {
                delta.name = fn.value("name", std::string{});
                delta.arguments = fn.value("arguments", std::string{});
            }
            if (handlers.on_tool_call) handlers.on_tool_call(delta);
        }
    }
    // usage
    const auto& usage = response["usage"];
    if (usage.is_object() && handlers.usage_out) {
        handlers.usage_out->prompt_tokens = usage.value("prompt_tokens", 0);
        handlers.usage_out->completion_tokens = usage.value("completion_tokens", 0);
        handlers.usage_out->total_tokens = usage.value("total_tokens", 0);
    }
    // stop 事件
    if (handlers.on_stop) {
        llm::StreamStopInfo stop;
        stop.stop_reason = choice0.value("finish_reason", "stop");
        handlers.on_stop(stop);
    }
}

// ─── 请求构建 ────────────────────────────────────────────────────

std::string OpenAiClient::build_body(const ChatRequest& request, bool stream) const {
    Json body = {{"model", settings_.llm.model}, {"temperature", settings_.llm.temperature},
                 {"max_tokens", settings_.llm.max_tokens}, {"messages", Json::array()}};
    if (stream) { body["stream"] = true; body["stream_options"] = Json::object({{"include_usage", true}}); }
    auto msgs = body["messages"];
    if (!request.system_prompt.empty()) msgs.push_back({{"role", "system"}, {"content", request.system_prompt}});
    msgs.push_back({{"role", "user"}, {"content", request.user_prompt}});
    return std::string(body.dump());
}

std::string OpenAiClient::build_body_with_tools(const ConversationHistory& history,
                                                  const capabilities::tool::ToolRegistry& tools,
                                                  const capabilities::tool::ToolChoiceConfig& tool_choice,
                                                  bool stream) const {
    Json body = {{"model", settings_.llm.model}, {"temperature", settings_.llm.temperature},
                 {"max_tokens", settings_.llm.max_tokens}, {"messages", history.to_openai_messages()}};
    if (stream) { body["stream"] = true; body["stream_options"] = Json::object({{"include_usage", true}}); }
    if (!tools.empty()) { body["tools"] = tools.to_openai_tools(); body["tool_choice"] = tool_choice.to_openai_format(); }
    return std::string(body.dump());
}

std::vector<std::string> OpenAiClient::build_headers() const {
    auto h = custom_headers(settings_);
    h.push_back(std::string("Authorization: Bearer ") + settings_.llm.api_key);
    return h;
}

ChatResult OpenAiClient::make_chat_result(const net::HttpResponse& resp) {
    std::string err; auto json = parse_json(resp.body, err);
    TokenUsage usage; if (err.empty()) usage = extract_openai_usage(json);
    std::string text;
    if (err.empty() && !json.empty()) {
        if (auto c = json.find("choices"); c != json.end() && c->is_array() && !c->empty())
            if (auto m = (*c)[0].find("message"); m != (*c)[0].end())
                if (auto t = get_json_value<std::string>(*m, "content")) text = *t;
        if (text.empty())
            if (auto c = json.find("content"); c != json.end() && c->is_array() && !c->empty())
                if (auto t = get_json_value<std::string>((*c)[0], "text")) text = *t;
    }
    return resp.status >= 200 && resp.status < 300
        ? ChatResult{resp.status, text, resp.body, {}, usage, {}}
        : ChatResult{resp.status, {}, resp.body, text, usage, {}};
}

std::string OpenAiClient::extract_text(std::string_view body) {
    std::string err; auto json = parse_json(body, err);
    if (!err.empty()) return {};
    if (auto e = json.find("error"); e != json.end() && e->is_object())
        if (auto m = get_json_value<std::string>(*e, "message")) return *m;
    if (auto c = json.find("choices"); c != json.end() && c->is_array() && !c->empty())
        if (auto m = (*c)[0].find("message"); m != (*c)[0].end())
            if (auto t = get_json_value<std::string>(*m, "content")) return *t;
    if (auto c = json.find("content"); c != json.end() && c->is_array() && !c->empty())
        if (auto t = get_json_value<std::string>((*c)[0], "text")) return *t;
    return {};
}

std::string OpenAiClient::request_body_for_test(const ChatRequest& r) const { return build_body(r, false); }
std::string OpenAiClient::stream_request_body_for_test(const ChatRequest& r) const { return build_body(r, true); }
std::vector<std::string> OpenAiClient::request_headers_for_test() const { return build_headers(); }

}  // namespace ben_gear::llm
