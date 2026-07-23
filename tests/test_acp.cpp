#include "test_framework.hpp"
#include "acp/core/types.hpp"
#include "acp/core/content_block.hpp"
#include "acp/core/message.hpp"

// 使用完整命名空间避免歧义
namespace acp = ben_gear::acp;
namespace base = ben_gear::base;
using ben_gear::Json;

// ==================== ContentBlock 测试 ====================

TEST(ContentBlockTest, TextBlock) {
    auto block = acp::ContentBlock::text("Hello, World!");
    
    EXPECT_TRUE(block.is_text());
    EXPECT_EQ(block.text(), "Hello, World!");
}

TEST(ContentBlockTest, ImageBlock) {
    acp::Source src;
    src.type = acp::SourceType::Base64;
    src.media_type = "image/png";
    src.data = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg==";
    
    auto block = acp::ContentBlock::image(src);
    
    EXPECT_TRUE(block.is_image());
    EXPECT_EQ(block.source().media_type, "image/png");
}

TEST(ContentBlockTest, ToolUseBlock) {
    ben_gear::acp::ToolCallRequest call;
    call.id = "call_123";
    call.name = "http_get";
    call.arguments = Json{{"url", "https://example.com"}};
    
    auto block = acp::ContentBlock::tool_use(call);
    
    EXPECT_TRUE(block.is_tool_use());
    EXPECT_EQ(block.tool_use().name, "http_get");
}

TEST(ContentBlockTest, ToolResultBlock) {
    ben_gear::acp::ToolCallResult result;
    result.tool_call_id = "call_123";
    result.name = "http_get";
    result.success = true;
    result.output = "Response data";
    
    auto block = acp::ContentBlock::tool_result(result);
    
    EXPECT_TRUE(block.is_tool_result());
    EXPECT_EQ(block.tool_result().output, "Response data");
}

TEST(ContentBlockTest, Serialization) {
    auto block = acp::ContentBlock::text("Test message");
    
    Json j = block.to_json();
    EXPECT_EQ(j["type"].get<std::string>(), "text");
    EXPECT_EQ(j["text"].get<std::string>(), "Test message");
    
    auto parsed = acp::ContentBlock::from_json(j);
    EXPECT_TRUE(parsed.is_text());
    EXPECT_EQ(parsed.text(), "Test message");
}

// ==================== ACPMessage 测试 ====================

TEST(ACPMessageTest, UserMessage) {
    auto msg = acp::ACPMessage::user_message("Hello");
    
    EXPECT_EQ(msg.role(), acp::Role::User);
    EXPECT_EQ(msg.content().size(), 1u);
    EXPECT_TRUE(msg.content()[0].is_text());
}

TEST(ACPMessageTest, AssistantMessage) {
    auto msg = acp::ACPMessage::assistant_message("Hi there!");
    
    EXPECT_EQ(msg.role(), acp::Role::Assistant);
    EXPECT_EQ(msg.get_all_text(), "Hi there!");
}

TEST(ACPMessageTest, AddContent) {
    acp::ACPMessage msg;
    msg.set_role(acp::Role::Assistant);
    
    msg.add_text("Part 1");
    msg.add_text("Part 2");
    
    EXPECT_EQ(msg.content().size(), 2u);
    EXPECT_EQ(msg.get_all_text(), "Part 1\nPart 2");
}

TEST(ACPMessageTest, ToolCalls) {
    acp::ACPMessage msg;
    msg.set_role(acp::Role::Assistant);
    
    ben_gear::acp::ToolCallRequest call;
    call.id = "call_1";
    call.name = "tool1";
    
    msg.add_tool_use(call);
    
    EXPECT_TRUE(msg.has_tool_calls());
    auto calls = msg.get_tool_calls();
    EXPECT_EQ(calls.size(), 1u);
    EXPECT_EQ(calls[0].name, "tool1");
}

TEST(ACPMessageTest, Serialization) {
    auto msg = acp::ACPMessage::user_message("Test message");
    
    Json j = msg.to_json();
    EXPECT_EQ(j["type"].get<std::string>(), "message");
    EXPECT_EQ(j["role"].get<std::string>(), "user");
    EXPECT_TRUE(j.contains("content"));
    
    auto parsed = acp::ACPMessage::from_json(j);
    EXPECT_EQ(parsed.role(), acp::Role::User);
    EXPECT_EQ(parsed.get_all_text(), "Test message");
}

// ==================== 集成测试 ====================

TEST(IntegrationTest, FullWorkflow) {
    // 1. 创建消息
    acp::ACPMessage msg;
    msg.set_role(acp::Role::User);
    msg.add_text("What's the weather in Beijing?");

    // 2. 序列化
    auto j = msg.to_json();
    auto json_str = j.dump();

    // 3. 解析
    auto parsed = acp::ACPMessage::from_json(j);
    EXPECT_EQ(parsed.get_all_text(), "What's the weather in Beijing?");

    // 4. 模拟助手响应
    acp::ACPMessage response;
    response.set_role(acp::Role::Assistant);

    ben_gear::acp::ToolCallRequest call;
    call.id = "call_123";
    call.name = "http_get";
    call.arguments = Json{{"url", "https://wttr.in/Beijing"}};
    response.add_tool_use(call);

    // 5. 验证工具调用
    EXPECT_TRUE(response.has_tool_calls());
    auto calls = response.get_tool_calls();
    EXPECT_EQ(calls[0].name, "http_get");
}
