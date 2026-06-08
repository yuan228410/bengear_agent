# ACP 协议使用指南

## 概述

ACP (Agent Communication Protocol) 是 BenGear 项目中的统一消息协议，用于 Agent 系统的通信。它支持：

- **多模态内容**：文本、图片、工具调用、工具结果
- **流式响应**：支持增量文本、思考过程、工具调用
- **会话管理**：会话 ID、消息 ID、时间戳
- **序列化**：JSON 格式的消息编解码

## 核心概念

### 1. 消息（ACPMessage）

消息是 ACP 协议的核心单元，包含：

```cpp
#include <ben_gear/acp/acp.hpp>

// 创建用户消息
auto msg = acp::ACPMessage::user_message("Hello, how are you?");

// 创建助手消息
auto response = acp::ACPMessage::assistant_message("I'm doing well!");

// 设置会话信息
msg.set_session_id("session_123");
msg.set_message_id("msg_456");
msg.set_current_timestamp();
```

### 2. 内容块（ContentBlock）

内容块是消息的基本组成单元，支持多种类型：

```cpp
// 文本内容
auto text_block = acp::ContentBlock::text("Hello");

// 图片内容
auto image_block = acp::ContentBlock::image("base64_encoded_image_data");

// 思考内容
auto thinking_block = acp::ContentBlock::thinking("Let me think...");

// 工具调用
llm::ToolCallRequest call;
call.id = "call_1";
call.name = "http_get";
call.arguments = Json{{"url", "https://api.example.com"}};
auto tool_block = acp::ContentBlock::tool_use(call);

// 工具结果
llm::ToolCallResult result;
result.tool_call_id = "call_1";
result.output = "Response data";
auto result_block = acp::ContentBlock::tool_result(result);
```

### 3. 消息构建

可以灵活构建复杂消息：

```cpp
acp::ACPMessage msg;
msg.set_role(acp::Role::Assistant);

// 添加多个内容块
msg.add_text("Let me check the weather for you.");
msg.add_thinking("User asked about weather in Beijing...");

// 添加工具调用
llm::ToolCallRequest call;
call.id = "call_123";
call.name = "get_weather";
call.arguments = Json{{"city", "Beijing"}};
msg.add_tool_use(call);

// 获取所有文本
std::string all_text = msg.get_all_text();

// 检查是否有工具调用
if (msg.has_tool_calls()) {
    auto calls = msg.get_tool_calls();
    // 处理工具调用
}
```

## 序列化与反序列化

### JSON 序列化

```cpp
#include <ben_gear/acp/codec/json_codec.hpp>

// 序列化
acp::JsonSerializer serializer;
auto json_str = serializer.serialize(message);

// 反序列化
acp::JsonParser parser;
base::container::String error;
auto parsed = parser.parse(json_str, error);

if (parsed.has_value()) {
    // 使用解析后的消息
}
```

### 直接 JSON 转换

```cpp
// 消息转 JSON
Json j = message.to_json();

// JSON 转消息
auto msg = acp::ACPMessage::from_json(j);
```

## 流式响应处理

### 1. 基本流式处理

```cpp
#include <ben_gear/acp/stream/dispatcher.hpp>

// 创建分发器
acp::StreamDispatcher dispatcher;

// 创建处理器
auto handler = std::make_shared<acp::CallbackStreamHandler>();

// 设置回调
handler->set_on_text([](const base::container::String& delta) {
    std::cout << "Text: " << delta << std::endl;
});

handler->set_on_thinking([](const base::container::String& delta) {
    std::cout << "Thinking: " << delta << std::endl;
});

handler->set_on_tool_call([](const llm::ToolCallRequest& call) {
    std::cout << "Tool call: " << call.name << std::endl;
});

// 注册处理器
dispatcher.add_handler(handler);

// 分发事件
dispatcher.dispatch_message_start();
dispatcher.dispatch_content_block_start(0, acp::ContentBlock::text(""));
dispatcher.dispatch_content_block_delta(0, "Hello");
dispatcher.dispatch_content_block_delta(0, " World");
dispatcher.dispatch_content_block_stop(0);
dispatcher.dispatch_message_stop();
```

### 2. 多处理器支持

```cpp
// 可以注册多个处理器
auto handler1 = std::make_shared<acp::CallbackStreamHandler>();
auto handler2 = std::make_shared<acp::CallbackStreamHandler>();

dispatcher.add_handler(handler1);
dispatcher.add_handler(handler2);

// 所有处理器都会收到事件
```

## Agent 集成

### 1. 消息转换

```cpp
#include <ben_gear/acp/adapter/agent_adapter.hpp>

acp::AgentAdapter adapter;

// ACP 消息 → Agent 消息
container::Vector<acp::ACPMessage> acp_messages;
// ... 填充消息
auto agent_messages = adapter.to_agent_messages(acp_messages);

// Agent 消息 → ACP 消息
llm::Message agent_msg = llm::Message::user("Hello");
auto acp_msg = adapter.to_acp_message(agent_msg);
```

### 2. 流式处理器适配

```cpp
// 创建 Agent 回调
agent::AgentCallbacks callbacks;

// 创建适配的流式处理器
auto handler = adapter.create_stream_handler(callbacks);

// 使用处理器处理流式响应
```

## 工具集成

### 1. 工具调用执行

```cpp
#include <ben_gear/acp/adapter/tool_adapter.hpp>

// 创建工具适配器
llm::ToolRegistry registry;
acp::ToolAdapter tool_adapter(registry);

// 执行单个工具调用
llm::ToolCallRequest call;
call.id = "call_1";
call.name = "http_get";
call.arguments = Json{{"url", "https://api.example.com"}};

auto result = tool_adapter.execute_tool(call);

// 批量执行
container::Vector<llm::ToolCallRequest> calls;
// ... 填充调用
auto results = tool_adapter.execute_tools(calls);
```

### 2. 工具定义转换

```cpp
// 获取所有工具定义（ACP 格式）
Json tools = tool_adapter.get_all_tools_acp();

// 单个工具定义转换
llm::ToolDefinition def;
def.name = "http_get";
def.description = "Make HTTP GET request";
Json acp_tool = tool_adapter.tool_definition_to_acp(def);
```

## 完整示例

### 示例 1：简单的对话

```cpp
#include <ben_gear/acp/acp.hpp>
#include <iostream>

int main() {
    // 创建用户消息
    auto user_msg = acp::ACPMessage::user_message("What's the weather in Beijing?");
    
    // 序列化
    acp::JsonSerializer serializer;
    auto json_str = serializer.serialize(user_msg);
    std::cout << "Serialized: " << json_str << std::endl;
    
    // 解析
    acp::JsonParser parser;
    base::container::String error;
    auto parsed = parser.parse(json_str, error);
    
    if (parsed.has_value()) {
        std::cout << "Parsed text: " << parsed->get_all_text() << std::endl;
    }
    
    return 0;
}
```

### 示例 2：工具调用流程

```cpp
#include <ben_gear/acp/acp.hpp>
#include <ben_gear/acp/adapter/tool_adapter.hpp>
#include <ben_gear/tool/registry.hpp>

int main() {
    // 创建工具注册表
    llm::ToolRegistry registry;
    
    // 注册工具
    registry.register_tool(
        "get_weather",
        "Get weather information for a city",
        {{"city", llm::ToolParameterSchema{
            .type = "string",
            .description = "City name"
        }}},
        [](const Json& args) -> base::container::String {
            std::string city = args["city"];
            return "Weather in " + city + ": Sunny, 25°C";
        }
    );
    
    // 创建工具适配器
    acp::ToolAdapter tool_adapter(registry);
    
    // 创建工具调用
    llm::ToolCallRequest call;
    call.id = "call_1";
    call.name = "get_weather";
    call.arguments = Json{{"city", "Beijing"}};
    
    // 执行工具
    auto result = tool_adapter.execute_tool(call);
    
    std::cout << "Tool result: " << result.output << std::endl;
    
    return 0;
}
```

### 示例 3：流式响应处理

```cpp
#include <ben_gear/acp/acp.hpp>
#include <ben_gear/acp/stream/dispatcher.hpp>
#include <iostream>

int main() {
    acp::StreamDispatcher dispatcher;
    
    std::string full_text;
    
    auto handler = std::make_shared<acp::CallbackStreamHandler>();
    handler->set_on_text([&full_text](const base::container::String& delta) {
        full_text += std::string(delta.data(), delta.size());
        std::cout << "Delta: " << delta << std::endl;
    });
    
    dispatcher.add_handler(handler);
    
    // 模拟流式响应
    dispatcher.dispatch_message_start();
    dispatcher.dispatch_content_block_start(0, acp::ContentBlock::text(""));
    dispatcher.dispatch_content_block_delta(0, "Hello");
    dispatcher.dispatch_content_block_delta(0, " ");
    dispatcher.dispatch_content_block_delta(0, "World");
    dispatcher.dispatch_content_block_stop(0);
    dispatcher.dispatch_message_stop();
    
    std::cout << "Full text: " << full_text << std::endl;
    
    return 0;
}
```

## 最佳实践

### 1. 消息设计

- **单一职责**：每条消息应该有明确的目的
- **内容分离**：文本、思考、工具调用使用不同的内容块
- **会话管理**：始终设置 session_id 和 message_id

### 2. 流式处理

- **增量处理**：使用流式处理器处理增量文本，避免缓冲整个响应
- **错误处理**：实现 on_error 回调处理异常情况
- **资源管理**：使用 shared_ptr 管理处理器生命周期

### 3. 工具集成

- **参数验证**：在工具执行前验证参数
- **错误友好**：返回 LLM 友好的错误信息
- **日志记录**：记录工具调用和结果

### 4. 性能优化

- **避免拷贝**：使用 string_view 和移动语义
- **内存复用**：复用消息对象和缓冲区
- **异步处理**：使用协程处理网络 I/O

## 架构图

```
┌─────────────────────────────────────────────────────────┐
│                    ACP 协议层                           │
├─────────────────────────────────────────────────────────┤
│  ACPMessage  │  ContentBlock  │  StreamDispatcher      │
├─────────────────────────────────────────────────────────┤
│              适配器层                                   │
│  AgentAdapter │ ToolAdapter │ JsonCodec                │
├─────────────────────────────────────────────────────────┤
│              Agent 系统                                 │
│  Agent │ ToolRegistry │ LLM Client                     │
└─────────────────────────────────────────────────────────┘
```

## 参考文档

- [ACP 协议规范](./acp_protocol_spec.md)
- [BenGear 架构设计](../README.md)
- [工具系统文档](./tool_system.md)
