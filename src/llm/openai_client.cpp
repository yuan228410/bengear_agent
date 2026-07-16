#include "llm/openai_client.hpp"
#include "base/log/logger.hpp"
#include "llm/usage_helpers.hpp"
#include "llm/retry.hpp"

namespace ben_gear::llm {

OpenAiClient::OpenAiClient(config::Settings settings, std::shared_ptr<net::HttpClient> http)
    : settings_(std::move(settings)),
      http_(http ? std::move(http)
                 : std::make_shared<net::HttpClient>(net::to_pool_config(settings_.connection_pool))),
      endpoint_url_(llm::endpoint_url(settings_, "/v1/chat/completions")) {}

// ─── 异步 API ────────────────────────────────────────────────────

net::Task<ChatResult> OpenAiClient::chat_async(net::EventLoop& loop, const ChatRequest& request,
                                                const net::CancellationToken& cancel) const {
    ensure_api_key();
    auto body = build_body(request, false);
    auto headers = build_headers();
    co_return co_await with_http_retry_async(loop, settings_, "openai chat_async",
        [&]() { return http_->post_json_async(loop, endpoint_url_, body, headers); },
        [](net::HttpResponse&& resp) -> ChatResult { return make_chat_result(resp); }, cancel);
}

net::Task<Json> OpenAiClient::chat_with_tools_async(net::EventLoop& loop,
                                                      const ConversationHistory& history,
                                                      const capabilities::tool::ToolRegistry& tools,
                                                      const capabilities::tool::ToolChoiceConfig& tool_choice,
                                                      const net::CancellationToken& cancel) const {
    ensure_api_key();
    auto body = build_body_with_tools(history, tools, tool_choice, false);
    auto headers = build_headers();
    co_return co_await with_http_retry_async(loop, settings_, "openai chat_with_tools_async",
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

net::Task<StreamResult> OpenAiClient::chat_stream_async(net::EventLoop& loop, const ChatRequest& request,
                                                          StreamHandlers handlers,
                                                          const net::CancellationToken& cancel) const {
    ensure_api_key();
    auto body = build_body(request, true);
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

net::Task<StreamResult> OpenAiClient::chat_stream_with_tools_async(
    net::EventLoop& loop, const ConversationHistory& history, const capabilities::tool::ToolRegistry& tools,
    const capabilities::tool::ToolChoiceConfig& tool_choice, StreamHandlers handlers,
    const net::CancellationToken& cancel) const {
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

// ─── 请求构建 ────────────────────────────────────────────────────

std::string OpenAiClient::build_body(const ChatRequest& request, bool stream) const {
    Json body = {{"model", settings_.model}, {"temperature", settings_.temperature},
                 {"max_tokens", settings_.max_tokens}, {"messages", Json::array()}};
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
    Json body = {{"model", settings_.model}, {"temperature", settings_.temperature},
                 {"max_tokens", settings_.max_tokens}, {"messages", history.to_openai_messages()}};
    if (stream) { body["stream"] = true; body["stream_options"] = Json::object({{"include_usage", true}}); }
    if (!tools.empty()) { body["tools"] = tools.to_openai_tools(); body["tool_choice"] = tool_choice.to_openai_format(); }
    return std::string(body.dump());
}

std::vector<std::string> OpenAiClient::build_headers() const {
    auto h = custom_headers(settings_);
    h.push_back(std::string("Authorization: Bearer ") + settings_.api_key);
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
