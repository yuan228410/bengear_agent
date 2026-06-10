#include <gtest/gtest.h>
#include "ben_gear/llm/usage.hpp"
#include "ben_gear/llm/ttfb_capture.hpp"
#include "ben_gear/llm/usage_helpers.hpp"
#include "ben_gear/llm/chat.hpp"
#include "ben_gear/base/utils/json.hpp"

using namespace ben_gear::llm;

// ==================== TokenUsage ====================

TEST(TokenUsageTest, DefaultZero) {
    TokenUsage u;
    EXPECT_EQ(u.prompt_tokens, 0);
    EXPECT_EQ(u.completion_tokens, 0);
    EXPECT_EQ(u.total_tokens, 0);
    EXPECT_EQ(u.cached_tokens, 0);
    EXPECT_TRUE(u.empty());
}

TEST(TokenUsageTest, NonEmpty) {
    TokenUsage u{.prompt_tokens = 100, .completion_tokens = 50, .total_tokens = 150};
    EXPECT_FALSE(u.empty());
    EXPECT_EQ(u.total_tokens, 150);
}

// ==================== RequestLatency ====================

TEST(RequestLatencyTest, DefaultNoTtfb) {
    RequestLatency l;
    EXPECT_EQ(l.total_seconds, 0.0);
    EXPECT_EQ(l.ttfb_seconds, 0.0);
    EXPECT_FALSE(l.has_ttfb);
}

// ==================== UsageTracker ====================

TEST(UsageTrackerTest, RecordAndRead) {
    UsageTracker tracker;

    TokenUsage u{.prompt_tokens = 100, .completion_tokens = 50, .total_tokens = 150, .cached_tokens = 20};
    RequestLatency l{.total_seconds = 1.5, .ttfb_seconds = 0.3, .has_ttfb = true};
    tracker.record(u, l);

    EXPECT_EQ(tracker.request_count(), 1);
    EXPECT_EQ(tracker.total_prompt_tokens(), 100);
    EXPECT_EQ(tracker.total_completion_tokens(), 50);
    EXPECT_EQ(tracker.total_tokens(), 150);
    EXPECT_EQ(tracker.total_cached_tokens(), 20);
    EXPECT_DOUBLE_EQ(tracker.total_latency_seconds(), 1.5);
    EXPECT_DOUBLE_EQ(tracker.avg_latency_seconds(), 1.5);
    EXPECT_EQ(tracker.last_actual_prompt_tokens(), 100);
}

TEST(UsageTrackerTest, CumulativeStats) {
    UsageTracker tracker;

    tracker.record({.prompt_tokens = 100, .completion_tokens = 50, .total_tokens = 150}, {.total_seconds = 2.0});
    tracker.record({.prompt_tokens = 200, .completion_tokens = 80, .total_tokens = 280}, {.total_seconds = 3.0});

    EXPECT_EQ(tracker.request_count(), 2);
    EXPECT_EQ(tracker.total_prompt_tokens(), 300);
    EXPECT_EQ(tracker.total_completion_tokens(), 130);
    EXPECT_EQ(tracker.total_tokens(), 430);
    EXPECT_DOUBLE_EQ(tracker.total_latency_seconds(), 5.0);
    EXPECT_DOUBLE_EQ(tracker.avg_latency_seconds(), 2.5);
    EXPECT_EQ(tracker.last_actual_prompt_tokens(), 200);
}

TEST(UsageTrackerTest, Reset) {
    UsageTracker tracker;
    tracker.record({.prompt_tokens = 100, .completion_tokens = 50, .total_tokens = 150}, {.total_seconds = 1.0});
    tracker.reset();

    EXPECT_EQ(tracker.request_count(), 0);
    EXPECT_EQ(tracker.total_prompt_tokens(), 0);
    EXPECT_EQ(tracker.last_actual_prompt_tokens(), 0);
}

TEST(UsageTrackerTest, LastActualPromptTokens) {
    UsageTracker tracker;
    EXPECT_EQ(tracker.last_actual_prompt_tokens(), 0);

    // prompt_tokens=0 不更新
    tracker.record({.prompt_tokens = 0, .completion_tokens = 10, .total_tokens = 10}, {.total_seconds = 0.5});
    EXPECT_EQ(tracker.last_actual_prompt_tokens(), 0);

    // prompt_tokens>0 更新
    tracker.record({.prompt_tokens = 500, .completion_tokens = 20, .total_tokens = 520}, {.total_seconds = 1.0});
    EXPECT_EQ(tracker.last_actual_prompt_tokens(), 500);
}

TEST(UsageTrackerTest, EstimatedContextUsage) {
    UsageTracker tracker;
    EXPECT_EQ(tracker.estimated_context_usage(100), 0);

    tracker.record({.prompt_tokens = 500, .completion_tokens = 20, .total_tokens = 520}, {.total_seconds = 1.0});
    EXPECT_EQ(tracker.estimated_context_usage(100), 600);
    EXPECT_EQ(tracker.estimated_context_usage(0), 500);
}

// ==================== TtfbCapture ====================

TEST(TtfbCaptureTest, NoTokenNoTtfb) {
    TtfbCapture ttfb;
    auto start = std::chrono::steady_clock::now();
    auto latency = ttfb.build_latency(start);

    EXPECT_FALSE(latency.has_ttfb);
    EXPECT_GE(latency.total_seconds, 0.0);
}

TEST(TtfbCaptureTest, CapturesTtfb) {
    TtfbCapture ttfb;
    auto start = std::chrono::steady_clock::now();

    auto wrapped = ttfb.wrap([](std::string_view) {});
    wrapped("hello");

    auto latency = ttfb.build_latency(start);

    EXPECT_TRUE(latency.has_ttfb);
    EXPECT_GT(latency.ttfb_seconds, 0.0);
    EXPECT_GE(latency.total_seconds, latency.ttfb_seconds);
}

TEST(TtfbCaptureTest, ForwardsToOriginal) {
    TtfbCapture ttfb;
    std::string collected;

    auto wrapped = ttfb.wrap([&](std::string_view token) { collected += token; });
    wrapped("abc");
    wrapped("def");

    EXPECT_EQ(collected, "abcdef");
    EXPECT_TRUE(ttfb.has_ttfb());
}

TEST(TtfbCaptureTest, OnlyFirstTokenCapturesTtfb) {
    TtfbCapture ttfb;
    auto start = std::chrono::steady_clock::now();

    auto wrapped = ttfb.wrap([](std::string_view) {});
    wrapped("a");
    auto ttfb1 = ttfb.build_latency(start).ttfb_seconds;

    wrapped("b");
    auto ttfb2 = ttfb.build_latency(start).ttfb_seconds;

    EXPECT_DOUBLE_EQ(ttfb1, ttfb2);
}

// ==================== ChatResult 工厂方法 ====================

TEST(ChatResultTest, ErrorFactory) {
    auto r = ChatResult::error(400, ben_gear::base::container::String("bad request"));
    EXPECT_EQ(r.status, 400);
    EXPECT_FALSE(r.error_message.empty());
    EXPECT_TRUE(r.text.empty());
    EXPECT_EQ(r.usage.total_tokens, 0);
    EXPECT_DOUBLE_EQ(r.latency.total_seconds, 0.0);
}

TEST(ChatResultTest, OkFactory) {
    auto r = ChatResult::ok(ben_gear::base::container::String("hello"), ben_gear::base::container::String("raw"));
    EXPECT_EQ(r.status, 200);
    EXPECT_EQ(std::string(r.text), "hello");
    EXPECT_EQ(std::string(r.raw), "raw");
    EXPECT_TRUE(r.error_message.empty());
}

TEST(ChatResultTest, DesignatedInitializer) {
    auto r = ChatResult{.status = 200, .text = ben_gear::base::container::String("ok"),
                        .usage = TokenUsage{.prompt_tokens = 100, .completion_tokens = 50, .total_tokens = 150},
                        .latency = RequestLatency{.total_seconds = 1.2, .ttfb_seconds = 0.3, .has_ttfb = true}};
    EXPECT_EQ(r.status, 200);
    EXPECT_EQ(r.usage.prompt_tokens, 100);
    EXPECT_TRUE(r.latency.has_ttfb);
}

// ==================== extract_*_usage ====================

TEST(ExtractUsageTest, OpenAIFormat) {
    auto json = ben_gear::parse_json(R"({
        "choices": [],
        "usage": {
            "prompt_tokens": 100,
            "completion_tokens": 50,
            "total_tokens": 150,
            "prompt_tokens_details": {"cached_tokens": 30}
        }
    })");
    auto usage = extract_openai_usage(json);
    EXPECT_EQ(usage.prompt_tokens, 100);
    EXPECT_EQ(usage.completion_tokens, 50);
    EXPECT_EQ(usage.total_tokens, 150);
    EXPECT_EQ(usage.cached_tokens, 30);
}

TEST(ExtractUsageTest, AnthropicFormat) {
    auto json = ben_gear::parse_json(R"({
        "usage": {
            "input_tokens": 200,
            "output_tokens": 80,
            "cache_read_input_tokens": 40
        }
    })");
    auto usage = extract_anthropic_usage(json);
    EXPECT_EQ(usage.prompt_tokens, 200);
    EXPECT_EQ(usage.completion_tokens, 80);
    EXPECT_EQ(usage.total_tokens, 280);
    EXPECT_EQ(usage.cached_tokens, 40);
}

TEST(ExtractUsageTest, MissingUsage) {
    auto json = ben_gear::parse_json(R"({"choices": []})");
    auto usage = extract_openai_usage(json);
    EXPECT_TRUE(usage.empty());
}

TEST(ExtractUsageTest, AutoDetectOpenAI) {
    auto json = ben_gear::parse_json(R"({
        "usage": {"prompt_tokens": 50, "completion_tokens": 25, "total_tokens": 75}
    })");
    auto usage = extract_usage_auto(json);
    EXPECT_EQ(usage.prompt_tokens, 50);
}

TEST(ExtractUsageTest, AutoDetectAnthropic) {
    auto json = ben_gear::parse_json(R"({
        "usage": {"input_tokens": 60, "output_tokens": 30}
    })");
    auto usage = extract_usage_auto(json);
    EXPECT_EQ(usage.prompt_tokens, 60);
    EXPECT_EQ(usage.completion_tokens, 30);
}
