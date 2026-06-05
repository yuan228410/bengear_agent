#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "ben_gear/llm/openai_client.hpp"
#include "ben_gear/llm/anthropic_client.hpp"

using ::testing::HasSubstr;
using ::testing::Not;

TEST(OpenAiClient, RequestBody) {
    ben_gear::config::Settings settings;
    settings.api_key = "openai-key";
    settings.model = "openai-model";
    ben_gear::llm::ChatRequest request{"system text", "user text"};
    ben_gear::llm::OpenAiClient client(settings);

    const auto body = client.request_body_for_test(request);
    EXPECT_THAT(body, HasSubstr("\"role\":\"system\""));
    EXPECT_THAT(body, HasSubstr("\"role\":\"user\""));
    EXPECT_THAT(body, Not(HasSubstr("\"system\":")));
}

TEST(OpenAiClient, RequestHeaders) {
    ben_gear::config::Settings settings;
    settings.api_key = "openai-key";
    settings.model = "openai-model";
    ben_gear::llm::ChatRequest request{"system text", "user text"};
    ben_gear::llm::OpenAiClient client(settings);

    const auto headers = client.request_headers_for_test();
    ASSERT_FALSE(headers.empty());
    EXPECT_EQ(headers.back(), "Authorization: Bearer openai-key");
}

TEST(OpenAiClient, StreamRequestBody) {
    ben_gear::config::Settings settings;
    settings.api_key = "openai-key";
    settings.model = "openai-model";
    ben_gear::llm::ChatRequest request{"system text", "user text"};
    ben_gear::llm::OpenAiClient client(settings);

    EXPECT_THAT(client.stream_request_body_for_test(request), HasSubstr("\"stream\":true"));
}

TEST(AnthropicClient, RequestBody) {
    ben_gear::config::Settings settings;
    settings.provider = ben_gear::config::Provider::anthropic;
    settings.api_key = "anthropic-key";
    settings.model = "claude-model";
    ben_gear::llm::ChatRequest request{"system text", "user text"};
    ben_gear::llm::AnthropicClient client(settings);

    const auto body = client.request_body_for_test(request);
    EXPECT_THAT(body, HasSubstr("\"system\":\"system text\""));
    EXPECT_THAT(body, Not(HasSubstr("\"role\":\"system\"")));
}

TEST(AnthropicClient, RequestHeaders) {
    ben_gear::config::Settings settings;
    settings.provider = ben_gear::config::Provider::anthropic;
    settings.api_key = "anthropic-key";
    settings.model = "claude-model";
    ben_gear::llm::ChatRequest request{"system text", "user text"};
    ben_gear::llm::AnthropicClient client(settings);

    const auto headers = client.request_headers_for_test();
    auto it = std::find(headers.begin(), headers.end(), "x-api-key: anthropic-key");
    EXPECT_NE(it, headers.end());
    it = std::find(headers.begin(), headers.end(), "anthropic-version: 2026-01-01");
    EXPECT_NE(it, headers.end());
}

TEST(AnthropicClient, StreamRequestBody) {
    ben_gear::config::Settings settings;
    settings.provider = ben_gear::config::Provider::anthropic;
    settings.api_key = "anthropic-key";
    settings.model = "claude-model";
    ben_gear::llm::ChatRequest request{"system text", "user text"};
    ben_gear::llm::AnthropicClient client(settings);

    EXPECT_THAT(client.stream_request_body_for_test(request), HasSubstr("\"stream\":true"));
}
