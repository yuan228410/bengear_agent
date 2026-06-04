#pragma once

#include "ben_gear/config/settings.hpp"
#include "ben_gear/llm/internal/anthropic_parser.hpp"
#include "ben_gear/llm/chat.hpp"
#include "ben_gear/llm/http_helpers.hpp"
#include "ben_gear/llm/message.hpp"
#include "ben_gear/llm/retry.hpp"
#include "ben_gear/llm/stream.hpp"
#include "ben_gear/tool/registry.hpp"
#include "ben_gear/tool/types.hpp"
#include "ben_gear/base/net/http.hpp"

#include <string>
#include <utility>
#include <vector>

namespace ben_gear::llm {

class AnthropicClient {
public:
    explicit AnthropicClient(config::Settings settings) : settings_(std::move(settings)) {}

    // 简单聊天（无工具）
    ChatResult chat(const ChatRequest& request) const {
        return with_retry(settings_, "anthropic chat", [&] {
            const auto url = endpoint_url(settings_, "/v1/messages");
            auto response = http_.post_json(
                container::String(url.c_str()), build_body(request, false), build_headers());
            return ChatResult{response.status, std::string(extract_text(response.body)), response.body};
        });
    }

    net::Task<ChatResult> chat_async(net::EventLoop& loop, const ChatRequest& request) const {
        ensure_api_key();
        auto& retry_cfg = settings_.llm_request_retry;
        for (int attempt = 1; attempt <= retry_cfg.max_attempts; ++attempt) {
            const auto url = endpoint_url(settings_, "/v1/messages");
            auto response = co_await http_.post_json_async(loop,
                container::String(url.c_str()), build_body(request, false), build_headers());

            if (response.status >= 200 && response.status < 300) {
                if (attempt > 1) log::info_fmt("anthropic chat_async succeeded on attempt={}", attempt);
                co_return ChatResult{response.status, std::string(extract_text(response.body)), response.body};
            }
            if (!is_retryable_status(response.status) || attempt == retry_cfg.max_attempts) {
                log::error_fmt("anthropic chat_async failed status={} attempt={}/{}", response.status, attempt, retry_cfg.max_attempts);
                co_return ChatResult{response.status, std::string(extract_text(response.body)), response.body};
            }
            auto delay = retry_delay_ms(retry_cfg, attempt);
            log::warn_fmt("anthropic chat_async retryable status={} attempt={}/{} retry_in={}ms",
                          response.status, attempt, retry_cfg.max_attempts, delay);
            co_await loop.sleep_for(std::chrono::milliseconds(delay));
        }
        co_return ChatResult{0, "", ""};
    }

    // 带工具的聊天
    Json chat_with_tools(const ConversationHistory& history,
                         const ToolRegistry& tools,
                         const ToolChoiceConfig& tool_choice = {}) const {
        return with_retry(settings_, "anthropic chat with tools", [&]() -> Json {
            const auto url = endpoint_url(settings_, "/v1/messages");
            auto body = build_body_with_tools(history, tools, tool_choice, false);
            auto response = http_.post_json(
                container::String(url.c_str()),
                container::String(body.dump().c_str()),
                build_headers());

            std::string error;
            auto result = parse_json(response.body, error);
            if (!error.empty()) {
                log::error_fmt("anthropic chat_with_tools parse failed: status={} error={}", response.status, error);
            }
            return result;
        });
    }

    net::Task<Json> chat_with_tools_async(net::EventLoop& loop,
                                          const ConversationHistory& history,
                                          const ToolRegistry& tools,
                                          const ToolChoiceConfig& tool_choice = {}) const {
        ensure_api_key();
        auto& retry_cfg = settings_.llm_request_retry;
        for (int attempt = 1; attempt <= retry_cfg.max_attempts; ++attempt) {
            const auto url = endpoint_url(settings_, "/v1/messages");
            auto body = build_body_with_tools(history, tools, tool_choice, false);
            auto response = co_await http_.post_json_async(loop,
                container::String(url.c_str()),
                container::String(body.dump().c_str()),
                build_headers());

            std::string error;
            auto result = parse_json(response.body, error);

            if (response.status >= 200 && response.status < 300) {
                if (attempt > 1) log::info_fmt("anthropic chat_with_tools_async succeeded on attempt={}", attempt);
                if (!error.empty()) {
                    log::error_fmt("anthropic chat_with_tools_async parse failed: status={} error={}", response.status, error);
                }
                co_return result;
            }
            if (!is_retryable_status(response.status) || attempt == retry_cfg.max_attempts) {
                log::error_fmt("anthropic chat_with_tools_async failed status={} attempt={}/{}", response.status, attempt, retry_cfg.max_attempts);
                co_return result;
            }
            auto delay = retry_delay_ms(retry_cfg, attempt);
            log::warn_fmt("anthropic chat_with_tools_async retryable status={} attempt={}/{} retry_in={}ms",
                          response.status, attempt, retry_cfg.max_attempts, delay);
            co_await loop.sleep_for(std::chrono::milliseconds(delay));
        }
        co_return Json::object();
    }

    StreamResult chat_stream(const ChatRequest& request, const StreamTokenHandler& on_token) const {
        return chat_stream(request, StreamHandlers(on_token));
    }

    StreamResult chat_stream(const ChatRequest& request, StreamHandlers handlers) const {
        return with_retry(settings_, "anthropic stream chat", [&] {
            const auto url = endpoint_url(settings_, "/v1/messages");
            AnthropicStreamParser parser(handlers);
            auto response = http_.post_json_stream(
                container::String(url.c_str()), build_body(request, false), build_headers(),
                [&](std::string_view chunk) { parser.parse(chunk); });
            return StreamResult{response.status, response.body};
        });
    }

    /// 带工具的流式聊天
    StreamResult chat_stream_with_tools(const ConversationHistory& history,
                                         const ToolRegistry& tools,
                                         const ToolChoiceConfig& tool_choice,
                                         StreamHandlers handlers) const {
        ensure_api_key();
        return with_retry(settings_, "anthropic stream chat with tools", [&] {
            const auto url = endpoint_url(settings_, "/v1/messages");
            AnthropicStreamParser parser(handlers);
            auto body = build_body_with_tools(history, tools, tool_choice, true);
            auto response = http_.post_json_stream(
                container::String(url.c_str()),
                container::String(body.dump().c_str()),
                build_headers(),
                [&](std::string_view chunk) { parser.parse(chunk); });
            return StreamResult{response.status, response.body};
        });
    }

    net::Task<StreamResult> chat_stream_with_tools_async(net::EventLoop& loop,
                                                          const ConversationHistory& history,
                                                          const ToolRegistry& tools,
                                                          const ToolChoiceConfig& tool_choice,
                                                          StreamHandlers handlers) const {
        ensure_api_key();
        auto& retry_cfg = settings_.llm_request_retry;
        for (int attempt = 1; attempt <= retry_cfg.max_attempts; ++attempt) {
            const auto url = endpoint_url(settings_, "/v1/messages");
            AnthropicStreamParser parser(handlers);
            auto body = build_body_with_tools(history, tools, tool_choice, true);
            auto response = co_await http_.post_json_stream_async(loop,
                container::String(url.c_str()),
                container::String(body.dump().c_str()),
                build_headers(),
                [&](std::string_view chunk) { parser.parse(chunk); });

            if (response.status >= 200 && response.status < 300) {
                if (attempt > 1) log::info_fmt("anthropic chat_stream_with_tools_async succeeded on attempt={}", attempt);
                co_return StreamResult{response.status, response.body};
            }
            if (!is_retryable_status(response.status) || attempt == retry_cfg.max_attempts) {
                log::error_fmt("anthropic chat_stream_with_tools_async failed status={} attempt={}/{}", response.status, attempt, retry_cfg.max_attempts);
                co_return StreamResult{response.status, response.body};
            }
            auto delay = retry_delay_ms(retry_cfg, attempt);
            log::warn_fmt("anthropic chat_stream_with_tools_async retryable status={} attempt={}/{} retry_in={}ms",
                          response.status, attempt, retry_cfg.max_attempts, delay);
            co_await loop.sleep_for(std::chrono::milliseconds(delay));
        }
        co_return StreamResult{0, ""};
    }

    net::Task<StreamResult> chat_stream_async(net::EventLoop& loop, const ChatRequest& request, StreamHandlers handlers) const {
        ensure_api_key();
        auto& retry_cfg = settings_.llm_request_retry;
        for (int attempt = 1; attempt <= retry_cfg.max_attempts; ++attempt) {
            const auto url = endpoint_url(settings_, "/v1/messages");
            AnthropicStreamParser parser(handlers);
            auto response = co_await http_.post_json_stream_async(loop,
                container::String(url.c_str()), build_body(request, true), build_headers(),
                [&](std::string_view chunk) { parser.parse(chunk); });

            if (response.status >= 200 && response.status < 300) {
                if (attempt > 1) log::info_fmt("anthropic chat_stream_async succeeded on attempt={}", attempt);
                co_return StreamResult{response.status, response.body};
            }
            if (!is_retryable_status(response.status) || attempt == retry_cfg.max_attempts) {
                log::error_fmt("anthropic chat_stream_async failed status={} attempt={}/{}", response.status, attempt, retry_cfg.max_attempts);
                co_return StreamResult{response.status, response.body};
            }
            auto delay = retry_delay_ms(retry_cfg, attempt);
            log::warn_fmt("anthropic chat_stream_async retryable status={} attempt={}/{} retry_in={}ms",
                          response.status, attempt, retry_cfg.max_attempts, delay);
            co_await loop.sleep_for(std::chrono::milliseconds(delay));
        }
        co_return StreamResult{0, ""};
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

        return container::String(body.dump().c_str());
    }

    Json build_body_with_tools(const ConversationHistory& history,
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

        return body;
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

    static container::String extract_text(const std::string& body) {
        std::string error;
        auto json = parse_json(body, error);
        if (!error.empty()) {
            return container::String();
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
    net::HttpClient http_;
};

}  // namespace ben_gear::llm
