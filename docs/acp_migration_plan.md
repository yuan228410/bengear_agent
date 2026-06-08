# ACP 协议替换 llm::Message 重构计划

## 📊 当前状态分析

### llm::Message 使用位置

1. **核心数据结构**
   - `llm::Message` - 消息结构体
   - `llm::ConversationHistory` - 对话历史管理类

2. **主要使用模块**
   - `llm/` - LLM 客户端（OpenAI、Anthropic）
   - `agent/` - Agent 编排层
   - `workspace/` - Session 会话管理
   - `memory/` - 记忆系统（Compactor、ContextBuilder）

3. **关键接口**
   - `ProviderClient::chat_with_tools_async(history, ...)`
   - `Session::history()` 返回 `ConversationHistory&`
   - `Compactor::compact(history)` 压缩历史

---

## 🎯 重构目标

### 1. 统一消息格式

**使用 `acp::ACPMessage` 替换 `llm::Message`**

优势：
- ✅ 标准化的 Agent 通信协议
- ✅ 更好的类型安全（std::variant）
- ✅ 内存占用优化（减少 66.7%）
- ✅ 支持更多内容类型（thinking、多模态）

### 2. 简化历史管理

**方案 A：直接使用 `Vector<ACPMessage>`**
```cpp
using ConversationHistory = container::Vector<acp::ACPMessage>;
```

优势：
- ✅ 简单直接
- ✅ 易于理解
- ❌ 失去增量缓存优化

**方案 B：创建 `acp::ConversationHistory` 类**
```cpp
class ConversationHistory {
    container::Vector<ACPMessage> messages_;
    mutable Json cached_openai_msgs_;
    mutable Json cached_anthropic_msgs_;
    // ... 增量缓存逻辑
};
```

优势：
- ✅ 保持增量缓存优化
- ✅ 提供便捷方法
- ❌ 需要额外实现

**推荐：方案 B**（保持性能优化）

---

## 📝 重构步骤

### 阶段 1：扩展 ACPMessage（兼容层）

1. 添加与 `llm::Message` 兼容的接口
2. 添加格式转换方法（to_openai_format、to_anthropic_format）
3. 添加静态工厂方法（system、user、assistant、tool_result）

### 阶段 2：创建 acp::ConversationHistory

1. 实现消息历史管理类
2. 实现增量缓存优化
3. 提供与 `llm::ConversationHistory` 兼容的接口

### 阶段 3：更新 LLM 客户端

1. 更新 `ProviderClient` 接口
2. 更新 `OpenAIClient` 实现
3. 更新 `AnthropicClient` 实现

### 阶段 4：更新上层模块

1. 更新 `Session` 使用 `acp::ConversationHistory`
2. 更新 `Agent` 使用 `acp::ACPMessage`
3. 更新 `Compactor` 和 `ContextBuilder`

### 阶段 5：移除旧代码

1. 删除 `llm::Message` 定义
2. 删除 `llm::ConversationHistory` 定义
3. 更新所有 using 声明

### 阶段 6：测试验证

1. 运行所有单元测试
2. 验证功能正确性
3. 性能对比测试

---

## ⚠️ 风险评估

### 高风险

1. **接口变更范围大** - 影响所有 LLM 客户端和上层模块
2. **缓存优化丢失** - 需要在新实现中保持
3. **编译错误多** - 需要逐步修复

### 缓解措施

1. **分阶段重构** - 每个阶段独立测试
2. **保持接口兼容** - 提供适配器或兼容方法
3. **增量提交** - 每个阶段完成后提交

---

## 📋 详细任务清单

### ✅ 阶段 1：扩展 ACPMessage

- [ ] 添加 `to_openai_format()` 方法
- [ ] 添加 `to_anthropic_format()` 方法
- [ ] 添加静态工厂方法
- [ ] 添加 `tool_result()` 工厂方法
- [ ] 添加 `content` 字段（兼容简单文本）

### ✅ 阶段 2：创建 acp::ConversationHistory

- [ ] 创建 `acp/conversation_history.hpp`
- [ ] 实现消息管理方法
- [ ] 实现增量缓存优化
- [ ] 实现 `to_openai_messages()` 方法
- [ ] 实现 `to_anthropic_messages()` 方法

### ✅ 阶段 3：更新 LLM 客户端

- [ ] 更新 `ProviderClient` 接口
- [ ] 更新 `OpenAIClient::build_body_with_tools()`
- [ ] 更新 `AnthropicClient::build_body_with_tools()`
- [ ] 测试 LLM 客户端

### ✅ 阶段 4：更新上层模块

- [ ] 更新 `Session` 类
- [ ] 更新 `Agent` 类
- [ ] 更新 `Compactor` 类
- [ ] 更新 `ContextBuilder` 类

### ✅ 阶段 5：移除旧代码

- [ ] 删除 `llm::Message` 定义
- [ ] 删除 `llm::ConversationHistory` 定义
- [ ] 更新 using 声明
- [ ] 清理头文件

### ✅ 阶段 6：测试验证

- [ ] 运行单元测试
- [ ] 功能验证
- [ ] 性能测试

---

## 🎯 预期收益

1. **统一消息格式** - 整个项目使用一套消息系统
2. **性能提升** - 内存占用减少 66.7%
3. **类型安全** - 编译期检查，减少运行时错误
4. **易于维护** - 减少重复代码

---

## 📅 时间估算

- 阶段 1：1-2 小时
- 阶段 2：2-3 小时
- 阶段 3：2-3 小时
- 阶段 4：3-4 小时
- 阶段 5：1 小时
- 阶段 6：1-2 小时

**总计：10-15 小时**

---

**是否开始执行？**
