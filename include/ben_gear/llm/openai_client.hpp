#pragma once

#include "ben_gear/config/settings.hpp"
#include "ben_gear/llm/chat.hpp"
#include "ben_gear/llm/http_helpers.hpp"
#include "ben_gear/workspace/conversation_history.hpp"
#include "ben_gear/llm/internal/openai_parser.hpp"
#include "ben_gear/llm/provider_error.hpp"
#include "ben_gear/llm/retry.hpp"
#include "ben_gear/llm/stream.hpp"
#include "ben_gear/tool/registry.hpp"

#include "ben_gear/tool/types.hpp"
#include "ben_gear/base/net/http.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace ben_gear::llm {

/// OpenAI API 客户端
///
/// 提供 OpenAI 格式的 LLM API 异步调用接口，支持：
/// - 异步聊天（基于协程）
/// - 流式响应
/// - 工具调用
/// - 自动重试（429、500+）
///
/// 同步调用方通过 net::sync_wait(loop, client.chat_async(...)) 桥接
class OpenAiClient {
public:
    /// 构造函数
    /// @param settings LLM 配置
    /// @param http HTTP 客户端（可选，默认创建新实例）
    explicit OpenAiClient(config::Settings settings,
                          std::shared_ptr<net::HttpClient> http = nullptr)
        : settings_(std::move(settings)),
          http_(http ? std::move(http)
                     : std::make_shared<net::HttpClient>(net::to_pool_config(settings_.connection_pool))),
          endpoint_url_(llm::endpoint_url(settings_, "/v1/chat/completions")) {}

    /// 简单聊天（无工具）
    /// @param request 聊天请求
    /// @return 聊天结果
    /// 异步聊天（无工具）
    /// @param loop 事件循环
    /// @param request 聊天请求
    /// @return 聊天结果协程
    net::Task<ChatResult> chat_async(net::EventLoop& loop, const ChatRequest& request) const {
        ensure_api_key();
        auto body = build_body(request, false);
        auto headers = build_headers();

        co_return co_await with_http_retry_async(loop, settings_, "openai chat_async",
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

        co_return co_await with_http_retry_async(loop, settings_, "openai chat_with_tools_async",
            [&]() { return http_->post_json_async(loop, container::String(endpoint_url_.c_str()), body, headers); },
            [](net::HttpResponse&& resp) -> Json {
                std::string error;
                auto result = parse_json(resp.body, error);
                if (!error.empty()) {
                    log::error_fmt("openai chat_with_tools_async parse failed: status={} error={}", resp.status, error);
                }
                return result;
            });
    }

    /// 带工具的异步流式聊天
    net::Task<StreamResult> chat_stream_with_tools_async(net::EventLoop& loop,
                                                         const workspace::ConversationHistory& history,
                                                         const ToolRegistry& tools,
                                                         const ToolChoiceConfig& tool_choice,
                                                         StreamHandlers handlers) const {
        ensure_api_key();
        auto body = build_body_with_tools(history, tools, tool_choice, true);
        auto headers = build_headers();

        co_return co_await with_http_retry_async(loop, settings_, "openai chat_stream_with_tools_async",
            [&]() -> net::Task<net::HttpResponse> {
                OpenAiStreamParser parser(handlers);
                auto resp = co_await http_->post_json_stream_async(loop,
                    container::String(endpoint_url_.c_str()), body, headers,
                    [&](std::string_view chunk) {
                        if (!parser.stopped()) parser.parse(chunk);
                        return !parser.stopped();  // 停止信号：解析器已停止则通知 HTTP 层停止读取
                    });
                parser.finish();
                co_return resp;
            },
            [](net::HttpResponse&& resp) -> StreamResult {
                return {resp.status, resp.body};
            });
    }

    net::Task<StreamResult> chat_stream_async(net::EventLoop& loop, const ChatRequest& request, StreamHandlers handlers) const {
        ensure_api_key();
        auto body = build_body(request, true);
        auto headers = build_headers();

        co_return co_await with_http_retry_async(loop, settings_, "openai chat_stream_async",
            [&]() -> net::Task<net::HttpResponse> {
                OpenAiStreamParser parser(handlers);
                auto resp = co_await http_->post_json_stream_async(loop,
                    container::String(endpoint_url_.c_str()), body, headers,
                    [&](std::string_view chunk) {
                        if (!parser.stopped()) parser.parse(chunk);
                        return !parser.stopped();  // 停止信号：解析器已停止则通知 HTTP 层停止读取
                    });
                parser.finish();
                co_return resp;
            },
            [](net::HttpResponse&& resp) -> StreamResult {
                return {resp.status, resp.body};
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
            {"temperature", settings_.temperature},
            {"max_tokens", settings_.max_tokens},
            {"messages", Json::array()}
        };

        if (stream) {
            body["stream"] = true;
        }

        auto messages = body["messages"];
        if (!request.system_prompt.empty()) {
            messages.push_back({{"role", "system"}, {"content", request.system_prompt}});
        }
        messages.push_back({{"role", "user"}, {"content", request.user_prompt}});

        // 一次序列化，避免返回 Json 后调用方再 dump() 造成多余拷贝
        return container::String(body.dump());
    }

    // 返回预序列化的 container::String，与 build_body 接口一致
    // 调用方直接传给 HTTP 客户端，无需再次 dump()
    container::String build_body_with_tools(const workspace::ConversationHistory& history,
                                            const ToolRegistry& tools,
                                            const ToolChoiceConfig& tool_choice,
                                            bool stream) const {
        Json body = {
            {"model", settings_.model},
            {"temperature", settings_.temperature},
            {"max_tokens", settings_.max_tokens},
            {"messages", history.to_openai_messages()}
        };

        if (stream) {
            body["stream"] = true;
        }

        if (!tools.empty()) {
            body["tools"] = tools.to_openai_tools();
            body["tool_choice"] = tool_choice.to_openai_format();
        }

        // 一次序列化，避免调用方每次 body.dump() 产生冗余拷贝和重复序列化
        return container::String(body.dump());
    }

    container::Vector<container::String> build_headers() const {
        container::Vector<container::String> headers;
        auto std_headers = custom_headers(settings_);
        for (const auto& h : std_headers) {
            headers.push_back(container::String(h.c_str()));
        }
        headers.push_back(container::String(("Authorization: Bearer " + settings_.api_key).c_str()));
        return headers;
    }

    static ChatResult make_chat_result(const net::HttpResponse& resp) {
        auto extracted = extract_text(resp.body);
        if (resp.status >= 200 && resp.status < 300) {
            return {resp.status, std::string(extracted), resp.body, {}};
        }
        return {resp.status, {}, resp.body, std::string(extracted)};
   }

    static container::String extract_text(std::string_view body) {
        std::string error;
        auto json = parse_json(body, error);
        if (!error.empty()) {
            return container::String();
        }

        // 提取 API 错误信息
        if (auto err = json.find("error"); err != json.end() && err->is_object()) {
            if (auto msg = get_json_value<std::string>(*err, "message")) {
                return container::String(msg->c_str());
            }
        }

        if (auto choices = json.find("choices"); choices != json.end() && choices->is_array() && !choices->empty()) {
            if (auto message = (*choices)[0].find("message"); message != (*choices)[0].end()) {
                if (auto content = get_json_value<std::string>(*message, "content")) {
                    return container::String(content->c_str());
                }
            }
        }

        if (auto content = json.find("content"); content != json.end() && content->is_array() && !content->empty()) {
            if (auto text = get_json_value<std::string>((*content)[0], "text")) {
                return container::String(text->c_str());
            }
        }

        return {};
    }

    config::Settings settings_;
    std::shared_ptr<net::HttpClient> http_;
    const std::string endpoint_url_;  // 构造时预计算，避免每次请求重复解析
};

}  // namespace ben_gear::llm
