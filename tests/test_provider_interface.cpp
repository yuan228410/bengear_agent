#include "test_framework.hpp"
#include "llm/provider_interface.hpp"
#include "capabilities/tool/registry.hpp"

namespace {
namespace net = ben_gear::net;

using ben_gear::llm::IProviderClient;
using ben_gear::llm::ChatResult;
using ben_gear::llm::ChatRequest;
using ben_gear::llm::StreamResult;
using ben_gear::llm::StreamHandlers;
using ben_gear::llm::StreamStopInfo;
using ben_gear::Json;

/// Mock IProviderClient — records calls and returns preset values.
/// All tracking fields are mutable so const methods can update them.
class MockProviderClient : public IProviderClient {
public:
    // ── Preset return values ──────────────────────────────────────
    ChatResult chat_result = ChatResult::ok("mock response", "raw mock");
    Json tools_result = Json::parse(R"({"tool":"test_tool","result":"ok"})");
    StreamResult stream_result{200, "raw stream data", {}, {}, false};

    // ── Call counters (mutable for const override) ────────────────
    mutable int chat_async_calls = 0;
    mutable int chat_with_tools_calls = 0;
    mutable int chat_stream_calls = 0;
    mutable int chat_stream_with_tools_calls = 0;

    // ── Last received arguments ───────────────────────────────────
    mutable ChatRequest last_chat_request{"", ""};
    mutable std::string last_stop_reason;
    mutable std::vector<std::string> tokens_received;
    mutable std::vector<std::string> thinking_received;

    // ── IProviderClient interface ─────────────────────────────────

    net::Task<ChatResult> chat_async(
        net::EventLoop&, const ChatRequest& request,
        const net::CancellationToken& = {}) const override {
        ++chat_async_calls;
        last_chat_request = request;
        co_return chat_result;
    }

    net::Task<Json> chat_with_tools_async(
        net::EventLoop&, const ben_gear::llm::ConversationHistory&,
        const ben_gear::capabilities::tool::ToolRegistry&,
        const ben_gear::capabilities::tool::ToolChoiceConfig& = {},
        const net::CancellationToken& = {}) const override {
        ++chat_with_tools_calls;
        co_return tools_result;
    }

    net::Task<StreamResult> chat_stream_async(
        net::EventLoop&, const ChatRequest& request,
        StreamHandlers handlers,
        const net::CancellationToken& = {}) const override {
        ++chat_stream_calls;
        last_chat_request = request;

        // Simulate streaming: invoke handlers synchronously
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

    net::Task<StreamResult> chat_stream_with_tools_async(
        net::EventLoop&, const ben_gear::llm::ConversationHistory&,
        const ben_gear::capabilities::tool::ToolRegistry&,
        const ben_gear::capabilities::tool::ToolChoiceConfig&,
        StreamHandlers handlers,
        const net::CancellationToken& = {}) const override {
        ++chat_stream_with_tools_calls;

        if (handlers.on_token) {
            handlers.on_token("tool stream token");
            tokens_received.push_back("tool stream token");
        }

        co_return stream_result;
    }
};

}  // namespace

// ===================================================================
// Test: chat_async returns preset result and records the request
// ===================================================================
TEST(ProviderInterface, ChatAsyncReturnsPresetResult) {
    MockProviderClient mock;
    mock.chat_result = ben_gear::llm::ChatResult::ok("hello from mock");

    ben_gear::net::EventLoop loop;
    ben_gear::llm::ChatRequest request{"system prompt", "user prompt"};

    auto task = mock.chat_async(loop, request);
    EXPECT_FALSE(task.done());
    task.resume();
    ASSERT_TRUE(task.done());

    auto result = task.result();
    EXPECT_EQ(result.status, 200);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.text, "hello from mock");
    EXPECT_EQ(mock.chat_async_calls, 1);
    EXPECT_EQ(mock.last_chat_request.system_prompt, "system prompt");
    EXPECT_EQ(mock.last_chat_request.user_prompt, "user prompt");
}

// ===================================================================
// Test: chat_async returns error result
// ===================================================================
TEST(ProviderInterface, ChatAsyncReturnsErrorResult) {
    MockProviderClient mock;
    mock.chat_result = ben_gear::llm::ChatResult::error(500, "server error");

    ben_gear::net::EventLoop loop;
    ben_gear::llm::ChatRequest request{"sys", "usr"};

    auto task = mock.chat_async(loop, request);
    task.resume();

    auto result = task.result();
    EXPECT_EQ(result.status, 500);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(result.error_message, "server error");
}

// ===================================================================
// Test: chat_stream_async invokes all stream handlers and returns result
// ===================================================================
TEST(ProviderInterface, ChatStreamAsyncInvokesHandlers) {
    MockProviderClient mock;

    ben_gear::net::EventLoop loop;
    ben_gear::llm::ChatRequest request{"sys", "usr"};

    // Build handlers that capture output into local variables
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

    auto task = mock.chat_stream_async(loop, request, std::move(handlers));
    task.resume();
    ASSERT_TRUE(task.done());

    auto result = task.result();
    EXPECT_EQ(result.status, 200);
    EXPECT_EQ(result.raw, "raw stream data");

    // Handlers were invoked by the mock
    EXPECT_EQ(captured_tokens.size(), 2u);
    EXPECT_EQ(captured_tokens[0], "Hello");
    EXPECT_EQ(captured_tokens[1], " world");
    EXPECT_EQ(captured_thinking.size(), 1u);
    EXPECT_EQ(captured_thinking[0], "Let me think...");
    EXPECT_EQ(captured_stop, "end_turn");

    // Mock's own tracking also recorded
    EXPECT_EQ(mock.chat_stream_calls, 1);
    EXPECT_EQ(mock.tokens_received.size(), 2u);
    EXPECT_EQ(mock.thinking_received.size(), 1u);
    EXPECT_EQ(mock.last_stop_reason, "end_turn");
    EXPECT_EQ(mock.last_chat_request.system_prompt, "sys");
    EXPECT_EQ(mock.last_chat_request.user_prompt, "usr");
}

// ===================================================================
// Test: chat_with_tools_async returns preset JSON
// ===================================================================
TEST(ProviderInterface, ChatWithToolsAsyncReturnsPresetJson) {
    MockProviderClient mock;
    mock.tools_result = Json::parse(R"({"tool":"test_tool","args":{"key":"val"}})");

    ben_gear::net::EventLoop loop;
    ben_gear::llm::ConversationHistory history;
    ben_gear::capabilities::tool::ToolRegistry registry;

    auto task = mock.chat_with_tools_async(loop, history, registry);
    task.resume();
    ASSERT_TRUE(task.done());

    auto result = task.result();
    EXPECT_TRUE(result.is_object());
    EXPECT_EQ(result["tool"].as_string(), "test_tool");
    EXPECT_EQ(result["args"]["key"].as_string(), "val");
    EXPECT_EQ(mock.chat_with_tools_calls, 1);
}

// ===================================================================
// Test: chat_stream_with_tools_async invokes handlers
// ===================================================================
TEST(ProviderInterface, ChatStreamWithToolsAsyncInvokesHandlers) {
    MockProviderClient mock;

    ben_gear::net::EventLoop loop;
    ben_gear::llm::ConversationHistory history;
    ben_gear::capabilities::tool::ToolRegistry registry;
    ben_gear::capabilities::tool::ToolChoiceConfig tool_choice;

    std::vector<std::string> captured;
    StreamHandlers handlers;
    handlers.on_token = [&](std::string_view token) {
        captured.emplace_back(token);
    };

    auto task = mock.chat_stream_with_tools_async(loop, history, registry, tool_choice,
                                                   std::move(handlers));
    task.resume();
    ASSERT_TRUE(task.done());

    auto result = task.result();
    EXPECT_EQ(result.status, 200);
    EXPECT_EQ(captured.size(), 1u);
    EXPECT_EQ(captured[0], "tool stream token");
    EXPECT_EQ(mock.chat_stream_with_tools_calls, 1);
}

// ===================================================================
// Test: virtual dispatch through IProviderClient pointer
// ===================================================================
TEST(ProviderInterface, DispatchThroughBasePointer) {
    MockProviderClient mock;
    mock.chat_result = ben_gear::llm::ChatResult::ok("via pointer");
    mock.tools_result = Json::parse(R"({"dispatched":true})");

    IProviderClient* provider = &mock;
    ben_gear::net::EventLoop loop;

    // ── chat_async through base pointer ──
    {
        auto task = provider->chat_async(loop, ChatRequest{"a", "b"});
        task.resume();
        auto result = task.result();
        EXPECT_EQ(result.text, "via pointer");
        EXPECT_EQ(mock.chat_async_calls, 1);
    }

    // ── chat_with_tools_async through base pointer ──
    {
        ben_gear::llm::ConversationHistory history;
        ben_gear::capabilities::tool::ToolRegistry registry;
        auto task = provider->chat_with_tools_async(loop, history, registry);
        task.resume();
        auto result = task.result();
        EXPECT_TRUE(result["dispatched"].as_bool());
        EXPECT_EQ(mock.chat_with_tools_calls, 1);
    }

    // ── chat_stream_async through base pointer ──
    {
        StreamHandlers handlers;
        bool token_called = false;
        handlers.on_token = [&](std::string_view) { token_called = true; };

        auto task = provider->chat_stream_async(loop, ChatRequest{"x", "y"},
                                                 std::move(handlers));
        task.resume();
        auto result = task.result();
        EXPECT_EQ(result.status, 200);
        EXPECT_TRUE(token_called);
        EXPECT_EQ(mock.chat_stream_calls, 1);
    }

    // ── chat_stream_with_tools_async through base pointer ──
    {
        ben_gear::llm::ConversationHistory history;
        ben_gear::capabilities::tool::ToolRegistry registry;
        ben_gear::capabilities::tool::ToolChoiceConfig tool_choice;

        StreamHandlers handlers;
        bool tool_token_called = false;
        handlers.on_token = [&](std::string_view) { tool_token_called = true; };

        auto task = provider->chat_stream_with_tools_async(
            loop, history, registry, tool_choice, std::move(handlers));
        task.resume();
        auto result = task.result();
        EXPECT_EQ(result.status, 200);
        EXPECT_TRUE(tool_token_called);
        EXPECT_EQ(mock.chat_stream_with_tools_calls, 1);
    }
}
