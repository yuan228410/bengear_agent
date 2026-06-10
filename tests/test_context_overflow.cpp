#include <gtest/gtest.h>
#include "ben_gear/llm/provider_error.hpp"
#include "ben_gear/llm/chat.hpp"
#include "ben_gear/llm/stream.hpp"
#include "ben_gear/workspace/conversation_history.hpp"

using namespace ben_gear::llm;
using namespace ben_gear::workspace;

// ==================== detect_context_overflow ====================

TEST(ContextOverflowTest, DetectStatus400WithContextLength) {
    EXPECT_TRUE(detect_context_overflow(400,
        R"({"error":{"message":"context_length_exceeded","type":"invalid_request_error"}})"));
}

TEST(ContextOverflowTest, DetectStatus400WithoutContextLength) {
    EXPECT_FALSE(detect_context_overflow(400, R"({"error":{"message":"bad request"}})"));
}

TEST(ContextOverflowTest, Status200NeverOverflow) {
    EXPECT_FALSE(detect_context_overflow(200, R"({"context_length": 100})"));
}

TEST(ContextOverflowTest, Status429NeverOverflow) {
    EXPECT_FALSE(detect_context_overflow(429, R"({"error":"rate limit"})"));
}

TEST(ContextOverflowTest, EmptyBody) {
    EXPECT_FALSE(detect_context_overflow(400, ""));
}

TEST(ContextOverflowTest, AnthropicBodyFormat) {
    EXPECT_TRUE(detect_context_overflow(400,
        R"({"type":"error","error":{"type":"invalid_request_error","message":"prompt is too long: 210000 tokens > context_length 200000"}})"));
}

TEST(ContextOverflowTest, OpenAIBodyFormat) {
    EXPECT_TRUE(detect_context_overflow(400,
        R"({"error":{"message":"This model's maximum context length is 128000 tokens however you requested 150000","type":"invalid_request_error","param":null,"code":"context_length_exceeded"}})"));
}

// ==================== ChatResult is_context_overflow ====================

TEST(ContextOverflowTest, ChatResultDefaultFalse) {
    ChatResult result;
    EXPECT_FALSE(result.is_context_overflow);
}

TEST(ContextOverflowTest, ChatResultErrorDefaultFalse) {
    auto result = ChatResult::error(400, ben_gear::base::container::String("bad"));
    EXPECT_FALSE(result.is_context_overflow);
}

TEST(ContextOverflowTest, ChatResultSetOverflow) {
    ChatResult result;
    result.status = 400;
    result.is_context_overflow = true;
    EXPECT_TRUE(result.is_context_overflow);
}

// ==================== StreamResult is_context_overflow ====================

TEST(ContextOverflowTest, StreamResultDefaultFalse) {
    StreamResult result;
    EXPECT_FALSE(result.is_context_overflow);
}

TEST(ContextOverflowTest, StreamResultSetOverflow) {
    StreamResult result;
    result.status = 400;
    result.is_context_overflow = true;
    EXPECT_TRUE(result.is_context_overflow);
}

// ==================== ConversationHistory prune_config ====================

TEST(ContextOverflowTest, PruneConfigRoundTrip) {
    ConversationHistory history;

    ben_gear::config::ContextPruneSettings cfg;
    cfg.enabled = true;
    cfg.hard_prune_after = 5;
    cfg.max_tool_result_chars = 500;
    cfg.protect_recent = 2;
    cfg.soft_prune_lines = 3;

    history.set_prune_config(cfg);

    auto read_back = history.prune_config();
    EXPECT_EQ(read_back.enabled, true);
    EXPECT_EQ(read_back.hard_prune_after, 5);
    EXPECT_EQ(read_back.max_tool_result_chars, 500);
    EXPECT_EQ(read_back.protect_recent, 2);
    EXPECT_EQ(read_back.soft_prune_lines, 3);
}

TEST(ContextOverflowTest, PruneConfigModifyForRecovery) {
    ConversationHistory history;

    ben_gear::config::ContextPruneSettings original;
    original.hard_prune_after = 10;
    original.max_tool_result_chars = 2000;
    history.set_prune_config(original);

    // 模拟 L3 恢复：全量裁剪
    auto cfg = history.prune_config();
    cfg.hard_prune_after = 0;
    cfg.max_tool_result_chars = 400;
    history.set_prune_config(cfg);

    auto read_back = history.prune_config();
    EXPECT_EQ(read_back.hard_prune_after, 0);
    EXPECT_EQ(read_back.max_tool_result_chars, 400);
}

// ==================== classify_http_error 兼容性 ====================

TEST(ContextOverflowTest, ClassifyHttpErrorContextOverflow) {
    auto kind = classify_http_error(400,
        R"({"error":{"message":"context_length_exceeded","type":"invalid_request_error"}})");
    EXPECT_EQ(kind, ProviderErrorKind::context_overflow);
}

TEST(ContextOverflowTest, ClassifyHttpErrorBadRequest) {
    auto kind = classify_http_error(400, R"({"error":{"message":"invalid model"}})");
    EXPECT_EQ(kind, ProviderErrorKind::bad_request);
}
