#include "test_framework.hpp"
#include "llm/retry.hpp"
#include "llm/chat.hpp"

TEST(LlmRetry, RetryableStatus) {
    EXPECT_TRUE(ben_gear::llm::is_retryable_status(429));
    EXPECT_TRUE(ben_gear::llm::is_retryable_status(503));
    EXPECT_FALSE(ben_gear::llm::is_retryable_status(400));
}


