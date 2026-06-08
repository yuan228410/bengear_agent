# Session 更新计划

## 当前状态

Session 类目前使用 `llm::ConversationHistory`，需要更新为 `workspace::ConversationHistory`。

## 主要变更点

### 1. 头文件依赖
```cpp
// 旧
#include "ben_gear/llm/message.hpp"

// 新
#include "ben_gear/workspace/conversation_history.hpp"
#include "ben_gear/acp/core/message.hpp"
```

### 2. 成员变量
```cpp
// 旧
llm::ConversationHistory history_;

// 新
ConversationHistory history_;
```

### 3. 接口变更
```cpp
// 旧
llm::ConversationHistory& history() { return history_; }

// 新
ConversationHistory& history() { return history_; }
```

### 4. 消息访问
```cpp
// 旧
auto& msgs = history_.messages();
for (auto msg : msgs) {
    if (msg.role == llm::MessageRole::user) {
        auto content = std::string(msg.content.data(), msg.content.size());
    }
}

// 新
auto& msgs = history_.messages();
for (const auto& msg : msgs) {
    if (msg.role() == acp::Role::User) {
        auto content = msg.get_all_text();
    }
}
```

## 更新步骤

1. 更新头文件依赖
2. 更新成员变量类型
3. 更新消息访问代码
4. 更新 Compactor 调用
5. 测试验证

## 风险点

- Compactor 依赖 `llm::ConversationHistory`，需要同步更新
- 消息访问方式变化较大
- 需要确保所有调用点都更新

## 建议

由于 Session 和 Compactor 强耦合，建议：
1. 先更新 Compactor
2. 再更新 Session
3. 最后更新 Agent
