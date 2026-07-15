#pragma once

#include "base/config/settings.hpp"
#include "llm/chat.hpp"
#include "llm/http_helpers.hpp"
#include "llm/conversation_history.hpp"
#include "llm/internal/openai_parser.hpp"
#include "llm/provider_error.hpp"
#include "llm/stream.hpp"
#include "capabilities/tool/registry.hpp"
#include "capabilities/tool/types.hpp"
#include "base/net/http.hpp"

#include <memory>
#include <string>
#include <vector>

namespace ben_gear::llm {

/// OpenAI API 客户端 — 全部方法实现已在 openai_client.cpp
class OpenAiClient {
public:
    explicit OpenAiClient(config::Settings settings,
                          std::shared_ptr<net::HttpClient> http = nullptr);

    net::Task<ChatResult> chat_async(net::EventLoop& loop, const ChatRequest& request,
                                     const net::CancellationToken& cancel = {}) const;

    net::Task<Json> chat_with_tools_async(net::EventLoop& loop,
                                          const ConversationHistory& history,
                                          const ToolRegistry& tools,
                                          const ToolChoiceConfig& tool_choice = {},
                                          const net::CancellationToken& cancel = {}) const;

    net::Task<StreamResult> chat_stream_async(net::EventLoop& loop, const ChatRequest& request,
                                             StreamHandlers handlers,
                                             const net::CancellationToken& cancel = {}) const;

    net::Task<StreamResult> chat_stream_with_tools_async(net::EventLoop& loop,
                                                         const ConversationHistory& history,
                                                         const ToolRegistry& tools,
                                                         const ToolChoiceConfig& tool_choice,
                                                         StreamHandlers handlers,
                                                         const net::CancellationToken& cancel = {}) const;

    void ensure_api_key() const {
        if (settings_.api_key.empty())
            throw ProviderError(ProviderErrorKind::auth_error, 0, "missing api key");
    }

    std::string request_body_for_test(const ChatRequest& request) const;
    std::string stream_request_body_for_test(const ChatRequest& request) const;
    std::vector<std::string> request_headers_for_test() const;
    static std::string extract_text(std::string_view body);

private:
    std::string build_body(const ChatRequest& request, bool stream) const;
    std::string build_body_with_tools(const ConversationHistory& history,
                                      const ToolRegistry& tools,
                                      const ToolChoiceConfig& tool_choice,
                                      bool stream) const;
    std::vector<std::string> build_headers() const;
    static ChatResult make_chat_result(const net::HttpResponse& resp);

    config::Settings settings_;
    std::shared_ptr<net::HttpClient> http_;
    const std::string endpoint_url_;
};

}  // namespace ben_gear::llm
