# ACP 协议 API 文档

## 📚 概述

本文档说明如何使用新的 ACP 协议类型替代旧的 `llm::Message` 系统。

---

## 🔄 类型映射

| 旧类型（已删除） | 新类型 | 说明 |
|----------------|--------|------|
| `llm::Message` | `acp::ACPMessage` | 消息类型 |
| `llm::MessageRole` | `acp::Role` | 消息角色 |
| `llm::ConversationHistory` | `workspace::ConversationHistory` | 会话历史管理 |
| `llm::ContentBlock` | `acp::ContentBlock` | 内容块 |

---

## 🚀 快速开始

### 1. 创建消息

#### 旧代码（已删除）
```cpp
// ❌ 旧代码 - 不再可用
llm::Message msg = llm::Message::user("Hello");
```

#### 新代码（推荐）
```cpp
// ✅ 新代码
#include "ben_gear/acp/core/message.hpp"

auto msg = acp::ACPMessage::user_message("Hello");
```

---

### 2. 管理会话历史

#### 旧代码（已删除）
```cpp
// ❌ 旧代码 - 不再可用
llm::ConversationHistory history;
history.add_user("Hello");
history.add_assistant("Hi there!");
```

#### 新代码（推荐）
```cpp
// ✅ 新代码
#include "ben_gear/workspace/conversation_history.hpp"

workspace::ConversationHistory history;
history.add_user("Hello");
history.add_assistant("Hi there!");
```

---

### 3. 转换为 LLM 格式

#### 旧代码（已删除）
```cpp
// ❌ 旧代码 - 不再可用
Json openai_format = msg.to_openai_format();
Json anthropic_format = msg.to_anthropic_format();
```

#### 新代码（推荐）
```cpp
// ✅ 新代码
Json openai_format = history.to_openai_messages();
Json anthropic_format = history.to_anthropic_messages();
```

---

## 📖 详细用法

### acp::ACPMessage

#### 创建消息

```cpp
#include "ben_gear/acp/core/message.hpp"

// 用户消息
auto user_msg = acp::ACPMessage::user_message("What's the weather?");

// 助手消息
auto assistant_msg = acp::ACPMessage::assistant_message("I'll check for you.");

// 系统消息
auto system_msg = acp::ACPMessage::system_message("You are a helpful assistant.");

// 工具结果消息
llm::ToolCallResult result;
result.tool_call_id = "call_001";
result.output = "Sunny, 25°C";
auto tool_msg = acp::ACPMessage::tool_result_message(result);
```

#### 添加内容

```cpp
auto msg = acp::ACPMessage::assistant_message("Hello");

// 添加文本
msg.add_text("How can I help?");

// 添加工具调用
llm::ToolCallRequest call;
call.id = "call_001";
call.name = "get_weather";
call.arguments = Json{{"city", "Beijing"}};
msg.add_tool_use(call);

// 添加工具结果
llm::ToolCallResult result;
result.tool_call_id = "call_001";
result.output = "Sunny, 25°C";
msg.add_tool_result(result);
```

#### 获取内容

```cpp
auto msg = acp::ACPMessage::user_message("Hello");

// 获取所有文本
auto text = msg.get_all_text();

// 获取角色
auto role = msg.role();  // acp::Role::User

// 获取内容块
const auto& blocks = msg.content();
for (const auto& block : blocks) {
    if (block.is_text()) {
        std::cout << block.text() << std::endl;
    }
}
```

---

### workspace::ConversationHistory

#### 创建和管理历史

```cpp
#include "ben_gear/workspace/conversation_history.hpp"

workspace::ConversationHistory history;

// 添加消息
history.add_user("Hello");
history.add_assistant("Hi there!");
history.add_system("You are a helpful assistant.");

// 添加工具结果
history.add_tool_result("call_001", "get_weather", "Sunny, 25°C");

// 添加自定义消息
auto msg = acp::ACPMessage::user_message("Custom message");
history.add_message(msg);
```

#### 格式转换

```cpp
workspace::ConversationHistory history;
history.add_user("Hello");
history.add_assistant("Hi there!");

// 转换为 OpenAI 格式
Json openai_format = history.to_openai_messages();

// 转换为 Anthropic 格式
Json anthropic_format = history.to_anthropic_messages();

// 获取系统提示（Anthropic 用）
auto system_prompt = history.get_system_prompt();
```

#### 其他操作

```cpp
workspace::ConversationHistory history;

// 获取消息数量
auto count = history.size();

// 是否为空
bool empty = history.empty();

// 获取所有消息
const auto& messages = history.messages();

// 清空历史
history.clear();

// 使缓存失效
history.invalidate_cache();
```

---

### acp::ContentBlock

#### 创建内容块

```cpp
#include "ben_gear/acp/core/content_block.hpp"

// 文本块
auto text_block = acp::ContentBlock::text("Hello, World!");

// 思考块
auto thinking_block = acp::ContentBlock::thinking("Let me think...");

// 工具调用块
llm::ToolCallRequest call;
call.id = "call_001";
call.name = "get_weather";
call.arguments = Json{{"city", "Beijing"}};
auto tool_block = acp::ContentBlock::tool_use(call);

// 工具结果块
llm::ToolCallResult result;
result.tool_call_id = "call_001";
result.output = "Sunny, 25°C";
auto result_block = acp::ContentBlock::tool_result(result);
```

#### 检查类型

```cpp
auto block = acp::ContentBlock::text("Hello");

if (block.is_text()) {
    std::cout << "Text: " << block.text() << std::endl;
}

if (block.is_tool_use()) {
    auto call = block.tool_use();
    std::cout << "Tool: " << call.name << std::endl;
}

if (block.is_tool_result()) {
    auto result = block.tool_result();
    std::cout << "Result: " << result.output << std::endl;
}
```

---

## 🔧 高级用法

### 使用适配器

```cpp
#include "ben_gear/llm/adapter.hpp"

// 转换单个消息
auto msg = acp::ACPMessage::user_message("Hello");
Json openai_msg = llm::OpenAIAdapter::to_openai_format(msg);
Json anthropic_msg = llm::AnthropicAdapter::to_anthropic_format(msg);

// 转换整个历史
workspace::ConversationHistory history;
history.add_user("Hello");
history.add_assistant("Hi there!");

Json openai_messages = llm::OpenAIAdapter::to_openai_messages(history);
Json anthropic_messages = llm::AnthropicAdapter::to_anthropic_messages(history);
```

---

## 📊 性能对比

| 操作 | 旧系统 | 新系统 | 改进 |
|------|--------|--------|------|
| **内存占用** | 72 bytes | 32 bytes | 减少 55.6% |
| **创建速度** | 3080 μs | 586 μs | 快 5.3 倍 |
| **拷贝速度** | 690 μs | 249 μs | 快 2.8 倍 |

---

## 🚨 迁移注意事项

### 1. 头文件变更

```cpp
// ❌ 旧代码
#include "ben_gear/llm/message.hpp"

// ✅ 新代码
#include "ben_gear/acp/core/message.hpp"
#include "ben_gear/workspace/conversation_history.hpp"
```

### 2. 命名空间变更

```cpp
// ❌ 旧代码
using namespace ben_gear::llm;

// ✅ 新代码
using namespace ben_gear::acp;
using namespace ben_gear::workspace;
```

### 3. 方法名变更

```cpp
// ❌ 旧代码
llm::Message::user("Hello")
msg.to_openai_format()

// ✅ 新代码
acp::ACPMessage::user_message("Hello")
history.to_openai_messages()
```

---

## 📚 相关文档

- **`docs/acp_refactor_complete_report.md`** - 完整的重构报告
- **`docs/acp_refactor_summary.md`** - 重构总结
- **`examples/acp_example.cpp`** - 使用示例

---

## ✅ 验证

所有代码已通过测试验证：

```
[==========] Running 299 tests from 51 test suites
[  PASSED  ] 299 tests
```

---

**更新日期**：2026-06-08
**版本**：2.0.0
**状态**：✅ 稳定
