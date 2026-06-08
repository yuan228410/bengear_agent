#include <gtest/gtest.h>
#include "ben_gear/acp/acp.hpp"

// 使用完整命名空间避免歧义
namespace acp = ben_gear::acp;
namespace llm = ben_gear::llm;
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
    llm::ToolCallRequest call;
    call.id = "call_123";
    call.name = "http_get";
    call.arguments = Json{{"url", "https://example.com"}};
    
    auto block = acp::ContentBlock::tool_use(call);
    
    EXPECT_TRUE(block.is_tool_use());
    EXPECT_EQ(block.tool_use().name, "http_get");
}

TEST(ContentBlockTest, ToolResultBlock) {
    llm::ToolCallResult result;
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
    
    llm::ToolCallRequest call;
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

// ==================== 流式处理测试 ====================

TEST(StreamDispatcherTest, DispatchEvents) {
    acp::StreamDispatcher dispatcher;
    
    std::string received_text;
    auto handler = std::make_shared<acp::CallbackStreamHandler>();
    handler->set_on_text([&received_text](const base::container::String& delta) {
        received_text += std::string(delta.data(), delta.size());
    });
    
    dispatcher.add_handler(handler);
    
    // 分发事件
    dispatcher.dispatch_message_start();
    dispatcher.dispatch_content_block_start(0, acp::ContentBlock::text(""));
    dispatcher.dispatch_content_block_delta(0, "Hello");
    dispatcher.dispatch_content_block_delta(0, " World");
    dispatcher.dispatch_content_block_stop(0);
    dispatcher.dispatch_message_stop();
    
    EXPECT_EQ(received_text, "Hello World");
}

TEST(StreamDispatcherTest, MultipleHandlers) {
    acp::StreamDispatcher dispatcher;
    
    int call_count = 0;
    
    auto handler1 = std::make_shared<acp::CallbackStreamHandler>();
    handler1->set_on_text([&call_count](const base::container::String&) {
        call_count++;
    });
    
    auto handler2 = std::make_shared<acp::CallbackStreamHandler>();
    handler2->set_on_text([&call_count](const base::container::String&) {
        call_count++;
    });
    
    dispatcher.add_handler(handler1);
    dispatcher.add_handler(handler2);
    
    dispatcher.dispatch_content_block_start(0, acp::ContentBlock::text(""));
    dispatcher.dispatch_content_block_delta(0, "test");
    dispatcher.dispatch_content_block_stop(0);
    
    EXPECT_EQ(call_count, 2);  // 两个处理器都被调用
}

// ==================== JSON 编解码测试 ====================

TEST(JsonCodecTest, SerializeMessage) {
    acp::JsonSerializer serializer;
    
    auto msg = acp::ACPMessage::user_message("Test message");
    auto json_str = serializer.serialize(msg);
    
    EXPECT_FALSE(json_str.empty());
    EXPECT_NE(std::string(json_str.data(), json_str.size()).find("Test message"), 
              std::string::npos);
}

TEST(JsonCodecTest, ParseMessage) {
    acp::JsonParser parser;
    
    std::string json = R"({
        "type": "message",
        "role": "user",
        "content": [
            {"type": "text", "text": "Hello"}
        ]
    })";
    
    base::container::String error;
    auto msg = parser.parse(json, error);
    
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(msg->role(), acp::Role::User);
    EXPECT_EQ(msg->get_all_text(), "Hello");
}

TEST(JsonCodecTest, ParseInvalidJson) {
    acp::JsonParser parser;
    
    base::container::String error;
    auto msg = parser.parse("{invalid}", error);
    
    EXPECT_FALSE(msg.has_value());
    EXPECT_FALSE(error.empty());
}

// ==================== 集成测试 ====================

TEST(IntegrationTest, FullWorkflow) {
    // 1. 创建消息
    acp::ACPMessage msg;
    msg.set_role(acp::Role::User);
    msg.add_text("What's the weather in Beijing?");
    
    // 2. 序列化
    acp::JsonSerializer serializer;
    auto json_str = serializer.serialize(msg);
    
    // 3. 解析
    acp::JsonParser parser;
    base::container::String error;
    auto parsed = parser.parse(std::string(json_str.data(), json_str.size()), error);
    
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->get_all_text(), "What's the weather in Beijing?");
    
    // 4. 模拟助手响应
    acp::ACPMessage response;
    response.set_role(acp::Role::Assistant);
    
    llm::ToolCallRequest call;
    call.id = "call_123";
    call.name = "http_get";
    call.arguments = Json{{"url", "https://wttr.in/Beijing"}};
    response.add_tool_use(call);
    
    // 5. 验证工具调用
    EXPECT_TRUE(response.has_tool_calls());
    auto calls = response.get_tool_calls();
    EXPECT_EQ(calls[0].name, "http_get");
}
