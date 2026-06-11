#include "ben_gear/test/test_framework.hpp"
#include "ben_gear/llm/openai_client.hpp"
#include "ben_gear/llm/anthropic_client.hpp"

TEST(OpenAiStreamParser, ExtractsDeltaContent) {
    std::string text;
    ben_gear::llm::OpenAiStreamParser parser([&](std::string_view token) {
        text += token;
    });
    parser.parse("data: {\"choices\":[{\"delta\":{\"content\":\"你\"}}]}\n\n"
                 "data: {\"choices\":[{\"delta\":{\"content\":\"好\"}}]}\n\n"
                 "data: [DONE]\n\n");
    EXPECT_EQ(text, "你好");
}

TEST(OpenAiStreamParser, ExtractsThinkingContent) {
    std::string text;
    std::string thinking;
    ben_gear::llm::OpenAiStreamParser parser(ben_gear::llm::StreamHandlers(
        [&](std::string_view token) { text += token; },
        [&](std::string_view token) { thinking += token; }
    ));
    parser.parse("data: {\"choices\":[{\"delta\":{\"reasoning_content\":\"想\"}}]}\n\n");
    EXPECT_EQ(thinking, "想");
}

TEST(AnthropicStreamParser, ExtractsTextDeltas) {
    std::string text;
    ben_gear::llm::AnthropicStreamParser parser([&](std::string_view token) {
        text += token;
    });
    parser.parse("event: message_start\ndata: {\"type\":\"message_start\"}\n\n"
                 "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"delta\":{\"type\":\"text_delta\",\"text\":\"你\"}}\n\n"
                 "event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"delta\":{\"type\":\"text_delta\",\"text\":\"好\"}}\n\n");
    EXPECT_EQ(text, "你好");
}

TEST(AnthropicStreamParser, ExtractsThinkingDeltas) {
    std::string text;
    std::string thinking;
    ben_gear::llm::AnthropicStreamParser parser(ben_gear::llm::StreamHandlers(
        [&](std::string_view token) { text += token; },
        [&](std::string_view token) { thinking += token; }
    ));
    parser.parse("event: content_block_delta\ndata: {\"type\":\"content_block_delta\",\"delta\":{\"thinking\":\"思\"}}\n\n");
    EXPECT_EQ(thinking, "思");
}
