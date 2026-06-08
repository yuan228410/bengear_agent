# Session 和 Compactor 更新计划

## 目标
将 Session 和 Compactor 从使用 `llm::Message` 改为使用 `acp::ACPMessage` 和 `workspace::ConversationHistory`

## 更新步骤

### 1. Session 类更新

#### 当前状态
```cpp
class Session {
    llm::ConversationHistory history_;  // 旧版
};
```

#### 目标状态
```cpp
class Session {
    workspace::ConversationHistory history_;  // 新版
};
```

#### 需要更新的方法
1. `history()` 返回类型
2. `maybe_compact()` - 使用新的接口
3. `restore_from_db()` - 创建 ACPMessage 而非 llm::Message
4. 持久化方法 - 适配新的消息格式

### 2. Compactor 类更新

#### 当前状态
```cpp
class Compactor {
    llm::ConversationHistory compact(
        llm::ConversationHistory history, ...);
};
```

#### 目标状态
```cpp
class Compactor {
    workspace::ConversationHistory compact(
        workspace::ConversationHistory history, ...);
};
```

#### 需要更新的方法
1. `should_compact_local()` - 参数类型
2. `compact()` - 参数和返回类型
3. `split_rounds()` - 使用 ACPMessage
4. 内部 Round 结构体

### 3. ContextBuilder 更新

#### 当前状态
```cpp
static int64_t estimate_messages_tokens(
    const llm::ConversationHistory& history);
```

#### 目标状态
```cpp
static int64_t estimate_messages_tokens(
    const workspace::ConversationHistory& history);
```

## 依赖关系
```
Session → workspace::ConversationHistory → acp::ACPMessage
Compactor → workspace::ConversationHistory → acp::ACPMessage
ContextBuilder → workspace::ConversationHistory → acp::ACPMessage
```

## 实施顺序
1. 更新 ContextBuilder（底层）
2. 更新 Compactor（中层）
3. 更新 Session（上层）
4. 更新 Agent（最上层）
