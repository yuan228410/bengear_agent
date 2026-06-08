/**
 * @file acp_example.cpp
 * @brief ACP 协议使用示例
 * 
 * 本示例展示如何使用 ACP 协议进行消息处理、工具调用和流式响应处理。
 */

#include <ben_gear/acp/acp.hpp>
#include <ben_gear/acp/adapter/tool_adapter.hpp>
#include <ben_gear/acp/codec/json_codec.hpp>
#include <ben_gear/acp/stream/dispatcher.hpp>
#include <ben_gear/tool/registry.hpp>
#include <iostream>
#include <sstream>

using namespace ben_gear;

// ==================== 示例 1：基本消息操作 ====================

void example_basic_message() {
    std::cout << "\n=== 示例 1：基本消息操作 ===\n" << std::endl;
    
    // 创建用户消息
    auto user_msg = acp::ACPMessage::user_message("What's the weather in Beijing?");
    
    // 注意：会话信息现在由 workspace::ConversationHistory 管理
    // ACPMessage 只包含标准协议字段（role, content）
    
    std::cout << "用户消息：" << user_msg.get_all_text() << std::endl;
    std::cout << "消息角色：" << (user_msg.role() == acp::Role::User ? "user" : "other") << std::endl;
    
    // 创建助手消息
    auto assistant_msg = acp::ACPMessage::assistant_message(
        "I'll check the weather in Beijing for you.");
    
    std::cout << "助手消息：" << assistant_msg.get_all_text() << std::endl;
}

// ==================== 示例 2：内容块操作 ====================

void example_content_blocks() {
    std::cout << "\n=== 示例 2：内容块操作 ===\n" << std::endl;
    
    // 创建文本内容块
    auto text_block = acp::ContentBlock::text("Hello, World!");
    std::cout << "文本块类型：" << (text_block.is_text() ? "text" : "other") << std::endl;
    std::cout << "文本内容：" << text_block.text() << std::endl;
    
    // 创建思考内容块
    auto thinking_block = acp::ContentBlock::thinking("Let me think about this...");
    std::cout << "思考内容：" << thinking_block.text() << std::endl;
    
    // 创建工具调用内容块
    llm::ToolCallRequest call;
    call.id = "call_001";
    call.name = "get_weather";
    call.arguments = Json{{"city", "Beijing"}};
    
    auto tool_block = acp::ContentBlock::tool_use(call);
    std::cout << "工具调用：" << tool_block.tool_use().name << std::endl;
    
    // 创建工具结果内容块
    llm::ToolCallResult result;
    result.tool_call_id = "call_001";
    result.output = "Sunny, 25°C";
    
    auto result_block = acp::ContentBlock::tool_result(result);
    std::cout << "工具结果：" << result_block.tool_result().output << std::endl;
}

// ==================== 示例 3：消息序列化 ====================

void example_serialization() {
    std::cout << "\n=== 示例 3：消息序列化 ===\n" << std::endl;
    
    // 创建消息
    auto msg = acp::ACPMessage::user_message("Hello, how are you?");
    
    // 序列化为 JSON
    Json j = msg.to_json();
    std::cout << "JSON 格式：" << j.dump(2) << std::endl;
    
    // 使用序列化器
    acp::JsonSerializer serializer;
    auto json_str = serializer.serialize(msg);
    std::cout << "\n序列化字符串：" << json_str << std::endl;
    
    // 解析 JSON
    acp::JsonParser parser;
    base::container::String error;
    auto parsed = parser.parse(json_str, error);
    
    if (parsed.has_value()) {
        std::cout << "解析成功：" << parsed->get_all_text() << std::endl;
    } else {
        std::cout << "解析失败：" << error << std::endl;
    }
}

// ==================== 示例 4：工具调用 ====================

void example_tool_calls() {
    std::cout << "\n=== 示例 4：工具调用 ===\n" << std::endl;
    
    // 创建工具注册表
    llm::ToolRegistry registry;
    
    // 注册天气查询工具
    registry.register_tool(
        "get_weather",
        "Get weather information for a city",
        {{"city", llm::ToolParameterSchema{
            .type = "string",
            .description = "City name"
        }}},
        [](const Json& args) -> base::container::String {
            std::string city = args["city"].get<std::string>();
            return "Weather in " + city + ": Sunny, 25°C";
        }
    );
    
    // 注册 HTTP 工具
    registry.register_tool(
        "http_get",
        "Make an HTTP GET request",
        {{"url", llm::ToolParameterSchema{
            .type = "string",
            .description = "URL to fetch"
        }}},
        [](const Json& args) -> base::container::String {
            std::string url = args["url"].get<std::string>();
            return "Response from " + url + ": {\"status\": \"ok\"}";
        }
    );
    
    // 创建工具适配器
    acp::ToolAdapter tool_adapter(registry);
    
    // 创建工具调用
    llm::ToolCallRequest call;
    call.id = "call_001";
    call.name = "get_weather";
    call.arguments = Json{{"city", "Beijing"}};
    
    // 执行工具
    auto result = tool_adapter.execute_tool(call);
    std::cout << "工具执行结果：" << result.output << std::endl;
    
    // 批量执行工具
    base::container::Vector<llm::ToolCallRequest> calls;
    
    llm::ToolCallRequest call1;
    call1.id = "call_002";
    call1.name = "get_weather";
    call1.arguments = Json{{"city", "Shanghai"}};
    calls.push_back(call1);
    
    llm::ToolCallRequest call2;
    call2.id = "call_003";
    call2.name = "http_get";
    call2.arguments = Json{{"url", "https://api.example.com"}};
    calls.push_back(call2);
    
    auto results = tool_adapter.execute_tools(calls);
    std::cout << "\n批量执行结果：" << std::endl;
    for (const auto& r : results) {
        std::cout << "  - " << r.output << std::endl;
    }
    
    // 获取工具定义
    Json tools = tool_adapter.get_all_tools_acp();
    std::cout << "\n工具定义：" << tools.dump(2) << std::endl;
}

// ==================== 示例 5：流式响应处理 ====================

void example_stream_processing() {
    std::cout << "\n=== 示例 5：流式响应处理 ===\n" << std::endl;
    
    // 创建分发器
    acp::StreamDispatcher dispatcher;
    
    // 收集完整文本
    std::string full_text;
    std::vector<std::string> thinking_parts;
    
    // 创建处理器
    auto handler = std::make_shared<acp::CallbackStreamHandler>();
    
    handler->set_on_text([&full_text](const base::container::String& delta) {
        full_text += std::string(delta.data(), delta.size());
        std::cout << "收到文本增量：" << delta << std::endl;
    });
    
    handler->set_on_thinking([&thinking_parts](const base::container::String& delta) {
        thinking_parts.push_back(std::string(delta.data(), delta.size()));
        std::cout << "收到思考增量：" << delta << std::endl;
    });
    
    // 注册处理器
    dispatcher.add_handler(handler);
    
    // 模拟流式响应
    dispatcher.dispatch_content_block_start(0, acp::ContentBlock::thinking(""));
    dispatcher.dispatch_content_block_delta(0, "Let me think...");
    dispatcher.dispatch_content_block_delta(0, " about the weather.");
    dispatcher.dispatch_content_block_stop(0);
    
    // 文本响应
    dispatcher.dispatch_content_block_start(1, acp::ContentBlock::text(""));
    dispatcher.dispatch_content_block_delta(1, "The weather");
    dispatcher.dispatch_content_block_delta(1, " in Beijing");
    dispatcher.dispatch_content_block_delta(1, " is sunny.");
    dispatcher.dispatch_content_block_stop(1);
    
    dispatcher.dispatch_message_stop();
    
    std::cout << "\n完整文本：" << full_text << std::endl;
    std::cout << "思考过程：";
    for (const auto& part : thinking_parts) {
        std::cout << part;
    }
    std::cout << std::endl;
}

// ==================== 示例 6：完整工作流 ====================

void example_full_workflow() {
    std::cout << "\n=== 示例 6：完整工作流 ===\n" << std::endl;
    
    // 1. 创建工具注册表
    llm::ToolRegistry registry;
    registry.register_tool(
        "get_weather",
        "Get weather information",
        {{"city", llm::ToolParameterSchema{
            .type = "string",
            .description = "City name"
        }}},
        []([[maybe_unused]] const Json& args) -> base::container::String {
            return "Weather: Sunny, 25°C";
        }
    );
    
    acp::ToolAdapter tool_adapter(registry);
    
    // 2. 创建用户消息
    auto user_msg = acp::ACPMessage::user_message("What's the weather in Beijing?");
    
    std::cout << "步骤 1：用户消息" << std::endl;
    std::cout << "  " << user_msg.get_all_text() << std::endl;
    
    // 3. 序列化消息
    acp::JsonSerializer serializer;
    auto json_str = serializer.serialize(user_msg);
    
    std::cout << "\n步骤 2：序列化消息" << std::endl;
    std::cout << "  " << json_str << std::endl;
    
    // 4. 模拟助手响应（包含工具调用）
    acp::ACPMessage assistant_msg;
    assistant_msg.set_role(acp::Role::Assistant);
    assistant_msg.add_text("Let me check the weather for you.");
    
    llm::ToolCallRequest call;
    call.id = "call_001";
    call.name = "get_weather";
    call.arguments = Json{{"city", "Beijing"}};
    assistant_msg.add_tool_use(call);
    
    std::cout << "\n步骤 3：助手响应（包含工具调用）" << std::endl;
    std::cout << "  文本：" << assistant_msg.get_all_text() << std::endl;
    std::cout << "  工具调用：" << call.name << std::endl;
    
    // 5. 执行工具
    auto tool_result = tool_adapter.execute_tool(call);
    
    std::cout << "\n步骤 4：执行工具" << std::endl;
    std::cout << "  结果：" << tool_result.output << std::endl;
    
    // 6. 创建工具结果消息
    acp::ACPMessage tool_msg;
    tool_msg.set_role(acp::Role::Tool);
    tool_msg.add_tool_result(tool_result);
    
    std::cout << "\n步骤 5：工具结果消息" << std::endl;
    std::cout << "  工具 ID：" << tool_result.tool_call_id << std::endl;
    
    // 7. 最终响应
    acp::ACPMessage final_msg = acp::ACPMessage::assistant_message(
        "The weather in Beijing is sunny with a temperature of 25°C.");
    
    std::cout << "\n步骤 6：最终响应" << std::endl;
    std::cout << "  " << final_msg.get_all_text() << std::endl;
}

// ==================== 主函数 ====================

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "   ACP 协议使用示例" << std::endl;
    std::cout << "========================================" << std::endl;
    
    try {
        example_basic_message();
        example_content_blocks();
        example_serialization();
        example_tool_calls();
        example_stream_processing();
        example_full_workflow();
        
        std::cout << "\n========================================" << std::endl;
        std::cout << "   所有示例执行完成！" << std::endl;
        std::cout << "========================================" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "错误：" << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
