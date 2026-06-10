#include <gtest/gtest.h>
#include "ben_gear/llm/retry.hpp"
#include "ben_gear/llm/chat.hpp"

TEST(LlmRetry, RetryableStatus) {
    EXPECT_TRUE(ben_gear::llm::is_retryable_status(429));
    EXPECT_TRUE(ben_gear::llm::is_retryable_status(503));
    EXPECT_FALSE(ben_gear::llm::is_retryable_status(400));
}

TEST(LlmRetry, RetryUntilSuccess) {
    ben_gear::config::Settings settings;
    settings.llm_request_retry.max_attempts = 3;
    settings.llm_request_retry.initial_delay_ms = 1;
    settings.llm_request_retry.max_delay_ms = 1;
    int attempts = 0;
    const auto result = ben_gear::llm::with_retry(settings, "test retry", [&] {
        ++attempts;
        return ben_gear::ChatResult{.status = attempts < 3 ? 429 : 200, .text = "ok", .raw = "raw"};
    });
    EXPECT_EQ(attempts, 3);
    EXPECT_EQ(result.status, 200);
    EXPECT_EQ(result.text, "ok");
}
