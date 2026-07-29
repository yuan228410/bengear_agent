#include "test_framework.hpp"
#include "llm/provider_interface.hpp"
#include "llm/chat.hpp"
#include "capabilities/tool/registry.hpp"

namespace {
namespace net = ben_gear::net;

using ben_gear::llm::IProviderClient;
using ben_gear::llm::StreamResult;
using ben_gear::llm::StreamHandlers;
using ben_gear::llm::StreamStopInfo;
using ben_gear::Json;

/// Mock IProviderClient — records calls and returns preset values.
class MockProviderClient : public IProviderClient {
public:
    // ── Preset return values ──────────────────────────────────────
    Json chat_result = Json::parse(R"({"choices":[{"message":{"content":"mock response"}}]})");
    StreamResult stream_result{200, "raw stream data", {}, {}, false};

    // ── Call counters ─────────────────────────────────────────────
    int chat_calls = 0;
    int chat_stream_calls = 0;
    int emit_calls = 0;

    // ── Last received arguments ───────────────────────────────────
    std::string last_stop_reason;
    std::vector<std::string> tokens_received;
    std::vector<std::string> thinking_received;

    // ── IProviderClient interface ─────────────────────────────────

    net::Task<StreamResult> chat_stream(
        net::EventLoop&, const ben_gear::llm::ConversationHistory&,
        const ben_gear::capabilities::tool::ToolRegistry&,
        const ben_gear::capabilities::tool::ToolChoiceConfig&,
        StreamHandlers handlers,
        const net::CancellationToken& = {}) override {
        ++chat_stream_calls;

        // 模拟流式：同步触发 handlers
        if (handlers.on_token) {
            handlers.on_token("Hello");
            tokens_received.push_back("Hello");
            handlers.on_token(" world");
            tokens_received.push_back(" world");
        }
        if (handlers.on_thinking) {
            handlers.on_thinking("Let me think...");
            thinking_received.push_back("Let me think...");
        }
        if (handlers.on_stop) {
            StreamStopInfo stop_info{"end_turn"};
            handlers.on_stop(stop_info);
            last_stop_reason = stop_info.stop_reason;
        }

        co_return stream_result;
    }

    net::Task<Json> chat(
        net::EventLoop&, const ben_gear::llm::ConversationHistory&,
        const ben_gear::capabilities::tool::ToolRegistry&,
        const ben_gear::capabilities::tool::ToolChoiceConfig& = {},
        const net::CancellationToken& = {}) override {
        ++chat_calls;
        co_return chat_result;
    }

    void emit_non_stream_result(const Json& response, StreamHandlers& handlers) override {
        ++emit_calls;
        // 模拟解析 JSON 并回调
        const auto& choices = response["choices"];
        if (choices.is_array() && !choices.empty()) {
            const auto& choice0 = choices[0];
            auto msg_it = choice0.find("message");
            if (msg_it != choice0.end() && msg_it->is_object()) {
                const auto& msg = *msg_it;
                auto content_it = msg.find("content");
                if (content_it != msg.end() && content_it->is_string() && handlers.on_token) {
                    handlers.on_token(content_it->get<std::string>());
                }
            }
        }
        if (handlers.on_stop) {
            handlers.on_stop(StreamStopInfo{"stop"});
        }
    }
};

}  // namespace

// ===================================================================
// Test: chat 返回预设 JSON
// ===================================================================
TEST(ProviderInterface, ChatReturnsPresetJson) {
    MockProviderClient mock;
    mock.chat_result = Json::parse(R"({"choices":[{"message":{"content":"hello from mock"}}]})");

    ben_gear::net::EventLoop loop;
    ben_gear::llm::ConversationHistory history;
    ben_gear::capabilities::tool::ToolRegistry registry;

    auto task = mock.chat(loop, history, registry);
    EXPECT_FALSE(task.done());
    task.resume();
    ASSERT_TRUE(task.done());

    auto result = task.result();
    EXPECT_TRUE(result.is_object());
    EXPECT_EQ(result["choices"][0]["message"]["content"].as_string(), "hello from mock");
    EXPECT_EQ(mock.chat_calls, 1);
}

// ===================================================================
// Test: chat_stream 触发所有 handlers 并返回结果
// ===================================================================
TEST(ProviderInterface, ChatStreamInvokesHandlers) {
    MockProviderClient mock;

    ben_gear::net::EventLoop loop;
    ben_gear::llm::ConversationHistory history;
    ben_gear::capabilities::tool::ToolRegistry registry;
    ben_gear::capabilities::tool::ToolChoiceConfig tool_choice;

    std::vector<std::string> captured_tokens;
    std::vector<std::string> captured_thinking;
    std::string captured_stop;

    StreamHandlers handlers;
    handlers.on_token = [&](std::string_view token) {
        captured_tokens.emplace_back(token);
    };
    handlers.on_thinking = [&](std::string_view thought) {
        captured_thinking.emplace_back(thought);
    };
    handlers.on_stop = [&](const StreamStopInfo& info) {
        captured_stop = info.stop_reason;
    };

    auto task = mock.chat_stream(loop, history, registry, tool_choice, std::move(handlers));
    task.resume();
    ASSERT_TRUE(task.done());

    auto result = task.result();
    EXPECT_EQ(result.status, 200);
    EXPECT_EQ(result.raw, "raw stream data");

    EXPECT_EQ(captured_tokens.size(), 2u);
    EXPECT_EQ(captured_tokens[0], "Hello");
    EXPECT_EQ(captured_tokens[1], " world");
    EXPECT_EQ(captured_thinking.size(), 1u);
    EXPECT_EQ(captured_thinking[0], "Let me think...");
    EXPECT_EQ(captured_stop, "end_turn");

    EXPECT_EQ(mock.chat_stream_calls, 1);
    EXPECT_EQ(mock.tokens_received.size(), 2u);
    EXPECT_EQ(mock.thinking_received.size(), 1u);
    EXPECT_EQ(mock.last_stop_reason, "end_turn");
}

// ===================================================================
// Test: emit_non_stream_result 解析 JSON 并触发 callbacks
// ===================================================================
TEST(ProviderInterface, EmitNonStreamResultParsesJson) {
    MockProviderClient mock;

    auto response = Json::parse(R"({"choices":[{"message":{"content":"parsed text","finish_reason":"stop"}}]})");

    std::string captured_text;
    std::string captured_stop;

    StreamHandlers handlers;
    handlers.on_token = [&](std::string_view token) { captured_text += token; };
    handlers.on_stop = [&](const StreamStopInfo& info) { captured_stop = info.stop_reason; };

    mock.emit_non_stream_result(response, handlers);

    EXPECT_EQ(captured_text, "parsed text");
    EXPECT_EQ(captured_stop, "stop");
    EXPECT_EQ(mock.emit_calls, 1);
}

// ===================================================================
// Test: 通过 IProviderClient 指针虚分发
// ===================================================================
TEST(ProviderInterface, DispatchThroughBasePointer) {
    MockProviderClient mock;
    mock.chat_result = Json::parse(R"({"dispatched":true})");

    IProviderClient* provider = &mock;
    ben_gear::net::EventLoop loop;

    // chat 虚分发
    {
        ben_gear::llm::ConversationHistory history;
        ben_gear::capabilities::tool::ToolRegistry registry;
        auto task = provider->chat(loop, history, registry);
        task.resume();
        auto result = task.result();
        EXPECT_TRUE(result["dispatched"].as_bool());
        EXPECT_EQ(mock.chat_calls, 1);
    }

    // chat_stream 虚分发
    {
        ben_gear::llm::ConversationHistory history;
        ben_gear::capabilities::tool::ToolRegistry registry;
        ben_gear::capabilities::tool::ToolChoiceConfig tool_choice;

        StreamHandlers handlers;
        bool token_called = false;
        handlers.on_token = [&](std::string_view) { token_called = true; };

        auto task = provider->chat_stream(loop, history, registry, tool_choice, std::move(handlers));
        task.resume();
        auto result = task.result();
        EXPECT_EQ(result.status, 200);
        EXPECT_TRUE(token_called);
        EXPECT_EQ(mock.chat_stream_calls, 1);
    }

    // emit_non_stream_result 虚分发
    {
        auto json = Json::parse(R"({"choices":[{"message":{"content":"via pointer"}}]})");
        StreamHandlers handlers;
        std::string text;
        handlers.on_token = [&](std::string_view t) { text += t; };

        provider->emit_non_stream_result(json, handlers);
        EXPECT_EQ(text, "via pointer");
        EXPECT_EQ(mock.emit_calls, 1);
    }
}
