# ACP 协议

ACP (Agent Communication Protocol) 是 BenGear 的统一消息协议层，位于 `src/acp/`。所有 LLM 通信、工具调用、流式响应均通过 ACP 进行格式转换。

## 协议规范

### 消息结构

```json
{
  "type": "message",
  "role": "user|assistant|system|tool",
  "content": [
    { "type": "text", "text": "Hello" }
  ],
  "session_id": "session_123",
  "message_id": "msg_456",
  "timestamp": "2024-01-01T00:00:00Z"
}
```

### 内容块类型

| 类型 | 说明 |
|------|------|
| `text` | 文本内容 |
| `image` | 图片（base64） |
| `thinking` | 思考过程 |
| `tool_use` | 工具调用请求（id + name + input） |
| `tool_result` | 工具执行结果（tool_use_id + content + is_error） |

### 流式事件

| 事件 | 说明 |
|------|------|
| `message_start` | 消息开始（含元数据） |
| `content_block_start` | 内容块开始（index + 初始内容） |
| `content_block_delta` | 增量内容（text_delta / input_json_delta） |
| `content_block_stop` | 内容块结束 |
| `message_stop` | 消息结束 |
| `error` | 错误（type + message） |

---

## 快速开始

### 创建消息

```cpp
#include "acp/acp.hpp"

// 工厂方法
auto user_msg = acp::ACPMessage::user_message("What's the weather?");
auto assistant_msg = acp::ACPMessage::assistant_message("Let me check.");
auto system_msg = acp::ACPMessage::system_message("You are a helpful assistant.");

// 手动构建
acp::ACPMessage msg;
msg.set_role(acp::Role::Assistant);
msg.add_text("Hello");
msg.add_thinking("User asked about weather...");
```

### 工具调用

```cpp
acp::ToolCallRequest call;
call.id = "call_001";
call.name = "get_weather";
call.arguments = Json{{"city", "Beijing"}};

msg.add_tool_use(call);

acp::ToolCallResult result;
result.tool_call_id = "call_001";
result.output = "Sunny, 25°C";
msg.add_tool_result(result);
```

### 查询内容

```cpp
std::string text = msg.get_all_text();
bool has_tools = msg.has_tool_calls();
auto calls = msg.get_tool_calls();
const auto& blocks = msg.content();
for (const auto& block : blocks) {
    if (block.is_text()) std::cout << block.text();
}
```

### ConversationHistory

```cpp
#include "llm/conversation_history.hpp"

llm::ConversationHistory history;
history.add_user("Hello");
history.add_assistant("Hi there!");

// 转换为 provider 格式
Json openai_fmt = history.to_openai_messages();
Json anthropic_fmt = history.to_anthropic_messages();
```

### Provider 适配器

```cpp
#include "llm/adapter.hpp"

// 单个消息转换
Json openai_msg = llm::OpenAIAdapter::to_openai_format(msg);
Json anthropic_msg = llm::AnthropicAdapter::to_anthropic_format(msg);

// 历史批量转换
Json msgs = llm::AnthropicAdapter::to_anthropic_messages(history);
std::string sys = llm::AnthropicAdapter::extract_system_prompt(history);
```

---

## API 参考

### 类型映射

| 类型 | 位置 |
|------|------|
| `acp::ACPMessage` | `acp/core/message.hpp` |
| `acp::ContentBlock` | `acp/core/content_block.hpp` |
| `acp::Role` | `acp/core/types.hpp` |
| `acp::ProtocolVersion` | `acp/core/types.hpp` |
| `acp::ToolCallRequest` | `acp/types/tool_call_types.hpp` |
| `acp::ToolCallResult` | `acp/types/tool_call_types.hpp` |
| `llm::ConversationHistory` | `llm/conversation_history.hpp` |
| `llm::OpenAIAdapter` | `llm/adapter.hpp` |
| `llm::AnthropicAdapter` | `llm/adapter.hpp` |

### 文件布局

```
src/acp/
├── acp.hpp              # 公共入口
├── core/
│   ├── types.hpp        # 枚举、基础类型 + ProtocolVersion
│   ├── content_block.hpp/cpp  # ContentBlock（variant）
│   └── message.hpp/cpp  # ACPMessage
├── types/
│   └── tool_call_types.hpp  # ToolCallRequest / ToolCallResult（从 capabilities/tool 迁入）
├── codec/
│   ├── serializer.hpp   # 协议无关序列化器
│   └── json_codec.hpp   # JSON 编解码
├── stream/
│   ├── handler.hpp      # StreamHandlers
│   └── dispatcher.hpp   # 流式事件分发
└── adapter/
    └── tool_adapter.hpp # 工具协议适配
```

### 包含路径

```cpp
#include "acp/acp.hpp"                    // 全部
#include "acp/core/content_block.hpp"    // ContentBlock
#include "acp/core/message.hpp"          // ACPMessage
#include "acp/types/tool_call_types.hpp" // ToolCallRequest / ToolCallResult
#include "acp/adapter/tool_adapter.hpp"  // ToolAdapter
```
