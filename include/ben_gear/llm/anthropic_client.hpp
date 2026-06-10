#pragma once

#include "ben_gear/config/settings.hpp"
#include "ben_gear/llm/chat.hpp"
#include "ben_gear/llm/http_helpers.hpp"
#include "ben_gear/workspace/conversation_history.hpp"
#include "ben_gear/llm/internal/anthropic_parser.hpp"
#include "ben_gear/llm/retry.hpp"
#include "ben_gear/llm/usage_helpers.hpp"
#include "ben_gear/llm/stream.hpp"
#include "ben_gear/tool/registry.hpp"

#include "ben_gear/tool/types.hpp"
#include "ben_gear/base/net/http.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ben_gear::llm {

class AnthropicClient {
public:
    explicit AnthropicClient(config::Settings settings,
                             std::shared_ptr<net::HttpClient> http = nullptr)
        : settings_(std::move(settings)),
          http_(http ? std::move(http)
                     : std::make_shared<net::HttpClient>(net::to_pool_config(settings_.connection_pool))),
          endpoint_url_(llm::endpoint_url(settings_, "/v1/messages")) {}

    net::Task<ChatResult> chat_async(net::EventLoop& loop, const ChatRequest& request) const {
        ensure_api_key();
        auto body = build_body(request, false);
        auto headers = build_headers();

        co_return co_await with_http_retry_async(loop, settings_, "anthropic chat_async",
            [&]() { return http_->post_json_async(loop, container::String(endpoint_url_.c_str()), body, headers); },
            [](net::HttpResponse&& resp) -> ChatResult {
                return make_chat_result(resp);
            });
    }

    net::Task<Json> chat_with_tools_async(net::EventLoop& loop,
                                          const workspace::ConversationHistory& history,
                                          const ToolRegistry& tools,
                                          const ToolChoiceConfig& tool_choice = {}) const {
        ensure_api_key();
        auto body = build_body_with_tools(history, tools, tool_choice, false);
        auto headers = build_headers();

        co_return co_await with_http_retry_async(loop, settings_, "anthropic chat_with_tools_async",
            [&]() { return http_->post_json_async(loop, container::String(endpoint_url_.c_str()), body, headers); },
            [](net::HttpResponse&& resp) -> Json {
                std::string error;
                auto result = parse_json(resp.body, error);
                if (!error.empty()) {
                    log::error_fmt("anthropic chat_with_tools_async parse failed: status={} error={}", resp.status, error);
                }
                return result;
            });
    }

    net::Task<StreamResult> chat_stream_with_tools_async(net::EventLoop& loop,
                                                        const workspace::ConversationHistory& history,
                                                        const ToolRegistry& tools,
                                                        const ToolChoiceConfig& tool_choice,
                                                        StreamHandlers handlers) const {
       ensure_api_key();
       auto body = build_body_with_tools(history, tools, tool_choice, true);
       auto headers = build_headers();
       auto usage_ptr = handlers.usage_out;

       co_return co_await with_http_retry_async(loop, settings_, "anthropic chat_stream_with_tools_async",
           [&]() -> net::Task<net::HttpResponse> {
                AnthropicStreamParser parser(handlers);
                auto resp = co_await http_->post_json_stream_async(loop,
                    container::String(endpoint_url_.c_str()), body, headers,
                    [&](std::string_view chunk) {
                        if (!parser.stopped()) parser.parse(chunk);
                        return true;
                    });
                parser.finish();
               co_return resp;
           },
           [usage_ptr](net::HttpResponse&& resp) -> StreamResult {
               StreamResult result;
               result.status = resp.status;
               result.raw = resp.body;
               if (usage_ptr) result.usage = *usage_ptr;
               return result;
          });
   }

   net::Task<StreamResult> chat_stream_async(net::EventLoop& loop, const ChatRequest& request, StreamHandlers handlers) const {
       ensure_api_key();
       auto body = build_body(request, true);
       auto headers = build_headers();
       auto usage_ptr = handlers.usage_out;

       co_return co_await with_http_retry_async(loop, settings_, "anthropic chat_stream_async",
           [&]() -> net::Task<net::HttpResponse> {
                AnthropicStreamParser parser(handlers);
                auto resp = co_await http_->post_json_stream_async(loop,
                    container::String(endpoint_url_.c_str()), body, headers,
                    [&](std::string_view chunk) {
                        if (!parser.stopped()) parser.parse(chunk);
                        return true;
                    });
                parser.finish();
               co_return resp;
           },
            [usage_ptr](net::HttpResponse&& resp) -> StreamResult {
                StreamResult result;
                result.status = resp.status;
                result.raw = resp.body;
                if (usage_ptr) result.usage = *usage_ptr;
                return result;
           });
   }

    void ensure_api_key() const {
        if (settings_.api_key.empty()) {
            throw std::runtime_error("missing api key");
        }
    }

    std::string request_body_for_test(const ChatRequest& request) const {
        return build_body(request, false).to_std_string();
    }

    std::string stream_request_body_for_test(const ChatRequest& request) const {
        return build_body(request, true).to_std_string();
    }

    std::vector<std::string> request_headers_for_test() const {
        auto headers = build_headers();
        std::vector<std::string> result;
        for (const auto& h : headers) {
            result.push_back(std::string(h));
        }
        return result;
    }

private:
    container::String build_body(const ChatRequest& request, bool stream) const {
        Json body = {
            {"model", settings_.model},
            {"max_tokens", settings_.max_tokens},
            {"temperature", settings_.temperature},
            {"messages", Json::array()}
        };

        if (stream) {
            body["stream"] = true;
        }

        if (!request.system_prompt.empty()) {
            body["system"] = request.system_prompt;
        }

        body["messages"].push_back({{"role", "user"}, {"content", request.user_prompt}});

        // 一次序列化，避免返回 Json 后调用方再 dump() 造成多余拷贝
        return container::String(body.dump());
    }

    // 返回预序列化的 container::String，与 build_body 接口一致
    container::String build_body_with_tools(const workspace::ConversationHistory& history,
                                            const ToolRegistry& tools,
                                            const ToolChoiceConfig& tool_choice,
                                            bool stream) const {
        Json body = {
            {"model", settings_.model},
            {"max_tokens", settings_.max_tokens},
            {"temperature", settings_.temperature},
            {"system", history.get_system_prompt()},
            {"messages", history.to_anthropic_messages()}
        };

        if (stream) {
            body["stream"] = true;
        }

        if (!tools.empty()) {
            body["tools"] = tools.to_anthropic_tools();
            body["tool_choice"] = tool_choice.to_anthropic_format();
        }

        // 一次序列化，避免调用方每次 body.dump() 产生冗余拷贝和重复序列化
        return container::String(body.dump());
    }

    std::string anthropic_version() const {
        auto v = settings_.anthropic_api_version;
        return v.empty() ? "2026-01-01" : std::string(v);
    }

    container::Vector<container::String> build_headers() const {
        container::Vector<container::String> headers;
        auto std_headers = custom_headers(settings_);
        for (const auto& h : std_headers) {
            headers.push_back(container::String(h.c_str()));
        }
        headers.push_back(container::String("x-api-key: ") + settings_.api_key);
        headers.push_back(container::String("anthropic-version: ") + container::String(anthropic_version().c_str()));
        return headers;
    }

    static ChatResult make_chat_result(const net::HttpResponse& resp) {
        auto extracted = extract_text(resp.body);
        // 从非流式响应提取 usage
        TokenUsage usage;
        std::string parse_err;
        auto json = parse_json(resp.body, parse_err);
        if (parse_err.empty()) {
            usage = extract_anthropic_usage(json);
        }
        if (resp.status >= 200 && resp.status < 300) {
            return {resp.status, std::string(extracted), resp.body, {}, usage, {}};
        }
        return {resp.status, {}, resp.body, std::string(extracted), usage, {}};
    }

    static container::String extract_text(std::string_view body) {
        std::string error;
        auto json = parse_json(body, error);
        if (!error.empty()) {
            return container::String();
        }

        if (auto err = json.find("error"); err != json.end() && err->is_object()) {
            if (auto msg = get_json_value<std::string>(*err, "message")) {
                return container::String(msg->c_str());
            }
        }

        if (auto content = json.find("content"); content != json.end() && content->is_array() && !content->empty()) {
            if (auto text = get_json_value<std::string>((*content)[0], "text")) {
                return container::String(text->c_str());
            }
        }

        if (auto choices = json.find("choices"); choices != json.end() && choices->is_array() && !choices->empty()) {
            if (auto message = (*choices)[0].find("message"); message != (*choices)[0].end()) {
                if (auto content = get_json_value<std::string>(*message, "content")) {
                    return container::String(content->c_str());
                }
            }
        }

        return {};
    }

    config::Settings settings_;
    std::shared_ptr<net::HttpClient> http_;
    const std::string endpoint_url_;  // 构造时预计算，避免每次请求重复解析
};

}  // namespace ben_gear::llm
