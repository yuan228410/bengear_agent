#include "llm/anthropic_client.hpp"
#include "log/logger.hpp"
#include "llm/usage_helpers.hpp"
#include "llm/retry.hpp"

namespace ben_gear::llm {

AnthropicClient::AnthropicClient(config::Settings settings, std::shared_ptr<net::HttpClient> http)
    : settings_(std::move(settings)),
      http_(std::move(http)),
      endpoint_url_(llm::endpoint_url(settings_, "/v1/messages")) {}

// ─── 核心方法 ────────────────────────────────────────────────────

net::Task<StreamResult> AnthropicClient::chat_stream(
    net::EventLoop& loop, const ConversationHistory& history, const capabilities::tool::ToolRegistry& tools,
    const capabilities::tool::ToolChoiceConfig& tool_choice, StreamHandlers handlers,
    const net::CancellationToken& cancel) {
    ensure_api_key();
    auto body = build_body_with_tools(history, tools, tool_choice, true);
    auto headers = build_headers();
    auto usage_ptr = handlers.usage_out;
    AnthropicStreamParser parser(std::move(handlers));
    auto resp = co_await http_->post_json_stream_async(loop, endpoint_url_, body, headers,
        [&](std::string_view chunk) {
            if (cancel.is_cancelled()) throw net::OperationCancelled("cancelled");
            if (!parser.stopped()) parser.parse(chunk);
            return true;
        });
    parser.finish();
    StreamResult r; r.status = resp.status; r.raw = resp.body;
    if (usage_ptr) r.usage = *usage_ptr;
    co_return r;
}

net::Task<Json> AnthropicClient::chat(net::EventLoop& loop,
                                      const ConversationHistory& history,
                                      const capabilities::tool::ToolRegistry& tools,
                                      const capabilities::tool::ToolChoiceConfig& tool_choice,
                                      const net::CancellationToken& cancel) {
    ensure_api_key();
    auto body = build_body_with_tools(history, tools, tool_choice, false);
    auto headers = build_headers();
    co_return co_await with_http_retry_async(loop, settings_, "anthropic chat",
        [&]() { return http_->post_json_async(loop, endpoint_url_, body, headers); },
        [](net::HttpResponse&& resp) -> Json {
            std::string error;
            auto result = parse_json(resp.body, error);
            if (!error.empty()) {
                log::error_fmt("anthropic parse failed: status={} error={}", resp.status, error);
                throw ProviderError(ProviderErrorKind::unknown, resp.status, "anthropic json parse: " + error);
            }
            return result;
        }, cancel);
}

/// 解析 Anthropic 非流式响应 JSON，通过 StreamHandlers 回调发出
/// Anthropic 格式: { content: [{ type: "text"/"thinking"/"tool_use", ... }], usage: {...}, stop_reason: "..." }
void AnthropicClient::emit_non_stream_result(const Json& response, StreamHandlers& handlers) {
    const auto& content_arr = response["content"];
    if (content_arr.is_array()) {
        for (const auto& block : content_arr) {
            auto type = block.value("type", "");
            if (type == "text") {
                auto text = block.value("text", "");
                if (!text.empty() && handlers.on_token) handlers.on_token(text);
            } else if (type == "thinking") {
                auto thinking = block.value("thinking", "");
                if (!thinking.empty() && handlers.on_thinking) handlers.on_thinking(thinking);
            } else if (type == "tool_use") {
                llm::StreamToolCallDelta delta;
                delta.index = block.value("index", 0);
                delta.id = block.value("id", "");
                delta.name = block.value("name", "");
                const auto& input = block["input"];
                delta.arguments = input.is_string() ? input.get<std::string>() : input.dump();
                if (handlers.on_tool_call) handlers.on_tool_call(delta);
            }
        }
    }
    // usage
    const auto& usage = response["usage"];
    if (usage.is_object() && handlers.usage_out) {
        handlers.usage_out->prompt_tokens = usage.value("input_tokens", 0);
        handlers.usage_out->completion_tokens = usage.value("output_tokens", 0);
        handlers.usage_out->total_tokens = handlers.usage_out->prompt_tokens + handlers.usage_out->completion_tokens;
    }
    // stop 事件
    if (handlers.on_stop) {
        llm::StreamStopInfo stop;
        stop.stop_reason = response.value("stop_reason", "end_turn");
        handlers.on_stop(stop);
    }
}

// ─── 请求构建 ────────────────────────────────────────────────────

std::string AnthropicClient::build_body(const ChatRequest& request, bool stream) const {
    Json body = {{"model", settings_.llm.model}, {"max_tokens", settings_.llm.max_tokens},
                 {"temperature", settings_.llm.temperature}, {"messages", Json::array()}};
    if (stream) body["stream"] = true;
    if (!request.system_prompt.empty()) body["system"] = request.system_prompt;
    body["messages"].push_back({{"role", "user"}, {"content", request.user_prompt}});
    return std::string(body.dump());
}

std::string AnthropicClient::build_body_with_tools(const ConversationHistory& history,
                                                     const capabilities::tool::ToolRegistry& tools,
                                                     const capabilities::tool::ToolChoiceConfig& tool_choice,
                                                     bool stream) const {
    Json body = {{"model", settings_.llm.model}, {"max_tokens", settings_.llm.max_tokens},
                 {"temperature", settings_.llm.temperature},
                 {"system", history.get_system_prompt()},
                 {"messages", history.to_anthropic_messages()}};
    if (stream) body["stream"] = true;
    if (!tools.empty()) { body["tools"] = tools.to_anthropic_tools(); body["tool_choice"] = tool_choice.to_anthropic_format(); }
    return std::string(body.dump());
}

std::string AnthropicClient::anthropic_version() const {
    return settings_.llm.anthropic_api_version.empty() ? std::string("2026-01-01") : settings_.llm.anthropic_api_version;
}

std::vector<std::string> AnthropicClient::build_headers() const {
    auto h = custom_headers(settings_);
    h.push_back(std::string("x-api-key: ") + settings_.llm.api_key);
    h.push_back(std::string("anthropic-version: ") + anthropic_version());
    return h;
}

ChatResult AnthropicClient::make_chat_result(const net::HttpResponse& resp) {
    std::string err; auto json = parse_json(resp.body, err);
    TokenUsage usage; if (err.empty()) usage = extract_anthropic_usage(json);
    std::string text;
    if (err.empty() && !json.empty()) {
        if (auto c = json.find("content"); c != json.end() && c->is_array() && !c->empty())
            if (auto t = get_json_value<std::string>((*c)[0], "text")) text = *t;
        if (text.empty())
            if (auto c = json.find("choices"); c != json.end() && c->is_array() && !c->empty())
                if (auto m = (*c)[0].find("message"); m != (*c)[0].end())
                    if (auto t = get_json_value<std::string>(*m, "content")) text = *t;
    }
    return resp.status >= 200 && resp.status < 300
        ? ChatResult{resp.status, text, resp.body, {}, usage, {}}
        : ChatResult{resp.status, {}, resp.body, text, usage, {}};
}

std::string AnthropicClient::extract_text(std::string_view body) {
    std::string err; auto json = parse_json(body, err);
    if (!err.empty()) return {};
    if (auto e = json.find("error"); e != json.end() && e->is_object())
        if (auto m = get_json_value<std::string>(*e, "message")) return *m;
    if (auto c = json.find("content"); c != json.end() && c->is_array() && !c->empty())
        if (auto t = get_json_value<std::string>((*c)[0], "text")) return *t;
    if (auto c = json.find("choices"); c != json.end() && c->is_array() && !c->empty())
        if (auto m = (*c)[0].find("message"); m != (*c)[0].end())
            if (auto t = get_json_value<std::string>(*m, "content")) return *t;
    return {};
}

std::string AnthropicClient::request_body_for_test(const ChatRequest& r) const { return build_body(r, false); }
std::string AnthropicClient::stream_request_body_for_test(const ChatRequest& r) const { return build_body(r, true); }
std::vector<std::string> AnthropicClient::request_headers_for_test() const { return build_headers(); }

}  // namespace ben_gear::llm
