# ACP 协议规范

## 概述

ACP (Agent Communication Protocol) 是 BenGear 项目中用于 Agent 系统通信的统一消息协议。它设计用于支持：

- 多模态内容（文本、图片、工具调用）
- 流式响应处理
- 会话管理
- 工具集成

## 消息格式

### 消息结构

```json
{
  "type": "message",
  "role": "user|assistant|system|tool",
  "content": [
    {
      "type": "text",
      "text": "Hello"
    }
  ],
  "session_id": "session_123",
  "message_id": "msg_456",
  "timestamp": "2024-01-01T00:00:00Z"
}
```

### 字段说明

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| type | string | 是 | 固定为 "message" |
| role | string | 是 | 消息角色：user, assistant, system, tool |
| content | array | 是 | 内容块数组 |
| session_id | string | 否 | 会话 ID |
| message_id | string | 否 | 消息 ID |
| timestamp | string | 否 | ISO 8601 时间戳 |

## 内容块类型

### 1. 文本内容

```json
{
  "type": "text",
  "text": "Hello, how can I help you?"
}
```

### 2. 图片内容

```json
{
  "type": "image",
  "source": {
    "type": "base64",
    "media_type": "image/jpeg",
    "data": "base64_encoded_image_data"
  }
}
```

### 3. 思考内容

```json
{
  "type": "thinking",
  "thinking": "Let me analyze the user's request..."
}
```

### 4. 工具调用

```json
{
  "type": "tool_use",
  "id": "call_123",
  "name": "http_get",
  "input": {
    "url": "https://api.example.com"
  }
}
```

### 5. 工具结果

```json
{
  "type": "tool_result",
  "tool_use_id": "call_123",
  "content": "Response data",
  "is_error": false
}
```

## 流式事件

### 事件类型

| 事件类型 | 说明 | 数据 |
|---------|------|------|
| message_start | 消息开始 | 消息元数据 |
| content_block_start | 内容块开始 | 内容块索引和初始内容 |
| content_block_delta | 内容块增量 | 增量文本 |
| content_block_stop | 内容块结束 | 内容块索引 |
| message_stop | 消息结束 | 无 |
| error | 错误 | 错误信息 |

### 事件格式

#### message_start

```json
{
  "type": "message_start",
  "message": {
    "id": "msg_123",
    "role": "assistant",
    "content": []
  }
}
```

#### content_block_start

```json
{
  "type": "content_block_start",
  "index": 0,
  "content_block": {
    "type": "text",
    "text": ""
  }
}
```

#### content_block_delta

```json
{
  "type": "content_block_delta",
  "index": 0,
  "delta": {
    "type": "text_delta",
    "text": "Hello"
  }
}
```

#### content_block_stop

```json
{
  "type": "content_block_stop",
  "index": 0
}
```

#### message_stop

```json
{
  "type": "message_stop"
}
```

#### error

```json
{
  "type": "error",
  "error": {
    "type": "overloaded_error",
    "message": "Service temporarily unavailable"
  }
}
```

## 工具定义格式

### 工具定义

```json
{
  "name": "http_get",
  "description": "Make an HTTP GET request",
  "input_schema": {
    "type": "object",
    "properties": {
      "url": {
        "type": "string",
        "description": "The URL to fetch"
      }
    },
    "required": ["url"]
  }
}
```

### 工具调用请求

```json
{
  "id": "call_123",
  "name": "http_get",
  "input": {
    "url": "https://api.example.com"
  }
}
```

### 工具调用结果

```json
{
  "tool_use_id": "call_123",
  "content": "Response data",
  "is_error": false
}
```

## 会话管理

### 会话 ID

- 用于标识一次完整的对话会话
- 建议使用 UUID v4 格式
- 在整个会话期间保持不变

### 消息 ID

- 用于标识单条消息
- 建议使用 UUID v4 格式
- 每条消息唯一

### 时间戳

- ISO 8601 格式：`YYYY-MM-DDTHH:MM:SSZ`
- 建议使用 UTC 时间

## 错误处理

### 错误类型

| 错误类型 | 说明 |
|---------|------|
| invalid_request_error | 请求格式错误 |
| authentication_error | 认证失败 |
| permission_error | 权限不足 |
| not_found_error | 资源不存在 |
| rate_limit_error | 请求频率超限 |
| overloaded_error | 服务过载 |
| internal_error | 内部错误 |

### 错误响应格式

```json
{
  "type": "error",
  "error": {
    "type": "invalid_request_error",
    "message": "Invalid message format"
  }
}
```

## 版本控制

### 当前版本

- **版本号**：1.0.0
- **状态**：稳定

### 版本兼容性

- 主版本号变更：不兼容的 API 变更
- 次版本号变更：向后兼容的功能新增
- 修订号变更：向后兼容的问题修复

## 实现指南

### 消息解析

1. 验证 JSON 格式
2. 检查必填字段
3. 验证字段类型
4. 解析内容块

### 流式处理

1. 按顺序处理事件
2. 维护内容块状态
3. 处理增量更新
4. 处理错误和中断

### 工具集成

1. 验证工具定义
2. 检查参数类型
3. 执行工具调用
4. 返回结果或错误

## 安全考虑

### 输入验证

- 验证所有输入字段
- 限制内容块数量
- 限制文本长度
- 验证图片大小

### 敏感信息

- 不在日志中记录敏感信息
- 过滤工具调用中的敏感参数
- 加密传输敏感数据

### 资源限制

- 限制消息大小
- 限制工具调用次数
- 限制并发连接数

## 性能优化

### 内存管理

- 使用移动语义避免拷贝
- 复用消息对象
- 及时释放资源

### 网络优化

- 使用压缩传输
- 批量处理消息
- 连接池复用

### 并发处理

- 使用线程池
- 异步 I/O
- 无锁数据结构

## 测试建议

### 单元测试

- 消息序列化/反序列化
- 内容块解析
- 流式事件处理
- 错误处理

### 集成测试

- 完整消息流程
- 工具调用流程
- 流式响应流程
- 错误恢复流程

### 性能测试

- 高并发场景
- 大消息处理
- 长时间运行
- 内存泄漏检测

## 参考资料

- [OpenAI Messages API](https://platform.openai.com/docs/api-reference/messages)
- [Anthropic Messages API](https://docs.anthropic.com/claude/reference/messages_post)
- [JSON Schema](https://json-schema.org/)
- [ISO 8601](https://www.iso.org/iso-8601-date-and-time-format.html)
