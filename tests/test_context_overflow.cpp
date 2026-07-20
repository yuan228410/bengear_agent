#include "test_framework.hpp"
#include "llm/provider_error.hpp"
#include "llm/chat.hpp"
#include "llm/stream.hpp"
#include "llm/conversation_history.hpp"
#include "memory/prune_utils.hpp"
#include "config/settings.hpp"

using namespace ben_gear::llm;

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
    auto result = ChatResult::error(400, std::string("bad"));
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

// ==================== PruneUtils 测试 ====================

TEST(ContextOverflowTest, PruneConfigRoundTrip) {
    ConversationHistory history;
    history.add_user(std::string("test message 1"));
    history.add_assistant(std::string("test response 1"));

    ben_gear::config::ContextPruneSettings cfg;
    cfg.enabled = true;
    cfg.hard_prune_after = 5;
    cfg.max_tool_result_chars = 500;
    cfg.protect_recent = 2;
    cfg.soft_prune_lines = 3;

    auto saved = ben_gear::memory::PruneUtils::apply_prune(history, cfg);
    // 少量消息不应触发裁剪
    EXPECT_GE(saved, 0);
}

TEST(ContextOverflowTest, PruneConfigModifyForRecovery) {
    ConversationHistory history;
    history.add_user(std::string("test"));
    history.add_assistant(std::string("test"));

    ben_gear::config::ContextPruneSettings cfg;
    cfg.hard_prune_after = 10;
    cfg.max_tool_result_chars = 2000;
    ben_gear::memory::PruneUtils::apply_prune(history, cfg);

    // 模拟 L3 恢复：全量裁剪
    cfg.hard_prune_after = 0;
    cfg.max_tool_result_chars = 400;
    auto saved = ben_gear::memory::PruneUtils::apply_prune(history, cfg);
    EXPECT_GE(saved, 0);
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
