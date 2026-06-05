# LLM 协议设计

## 提供商边界

`ProviderClient` 是协议分发边界。它检查配置的提供商并委托给协议特定的客户端：

- `OpenAiClient`
- `AnthropicClient`

Agent 只与 `ProviderClient` 对话，不知道提供商请求格式。

## OpenAI Chat Completions

OpenAI 风格的请求使用：

- 端点：`/v1/chat/completions`
- 认证头：`Authorization: Bearer <api_key>`
- 请求体字段：`model`、`temperature`、`max_tokens`、`messages`
- 流式字段：`"stream": true`

系统提示表示为 `system` 角色消息。

流式输出从 SSE 事件中解析 `choices[].delta.content`。推理输出从兼容字段（如 `reasoning_content` 或 `thinking`）中解析（如果存在）。

增量工具调用从 `choices[].delta.tool_calls[].function` 中解析。

## Anthropic Messages

Anthropic 风格的请求使用：

- 端点：`/v1/messages`
- 认证头：`x-api-key: <api_key>`
- 版本头：`anthropic-version: <version>`（默认 `2026-01-01`，可通过模型配置的 `anthropic_api_version` 字段覆盖）
- 请求体字段：`model`、`max_tokens`、`temperature`、`system`、`messages`
- 流式字段：`"stream": true`

系统提示使用 Anthropic 的顶层 `system` 字段。

流式输出从 SSE `content_block_delta` 事件中解析。文本增量从 `text` 读取；思考增量从 `thinking` 或 `thinking_delta` 读取（如果存在）。工具使用增量从 `input_json_delta` 中解析。

## 流式模型

流式 API 使用 `StreamHandlers`：

```cpp
struct StreamHandlers {
    StreamTokenHandler on_token;
    StreamThinkingHandler on_thinking;
    StreamToolCallHandler on_tool_call;  // StreamToolCallDelta
    StreamStopHandler on_stop;           // StreamStopInfo
};
```

这将正常响应 token、思考/推理 token 和增量工具调用分开处理。

### StreamToolCallDelta

```cpp
struct StreamToolCallDelta {
    int index;              // 工具调用索引
    std::string id;         // 工具调用 ID（首次增量）
    std::string name;       // 工具名称（首次增量）
    std::string arguments;  // 参数片段（累积）
};
```

在 `Agent::run_session_stream_step` 中，增量累积到 `PendingToolCall` 结构：

```cpp
struct PendingToolCall {
    container::String id;
    container::String name;
    container::String arguments;
};
std::map<int, PendingToolCall> pending_tools;
```

流式完成后，待处理工具转换为 `ToolCallRequest`，并解析 JSON 参数。

### StreamStopReason

解析器跟踪流式停止的原因：

```cpp
enum class StreamStopReason {
    none,           // 流仍在进行
    done,           // 收到 [DONE] 标记
    finish_stop,    // finish_reason: stop
    finish_tools,   // finish_reason: tool_calls
    error           // 解析错误
};
```

这帮助上层决定下一步操作：
- `done` / `finish_stop`：正常完成
- `finish_tools`：执行工具
- `error`：可能需要重试或降级

### HttpResponse.callback_stopped

当流式解析器提前停止（例如 `finish_reason: tool_calls`）时，HTTP 回调返回 `false` 以停止读取。`HttpResponse` 记录这一点：

```cpp
struct HttpResponse {
    int status = 0;
    std::string body;
    container::Map<container::String, std::string> headers;
    bool callback_stopped = false;  // 解析器提前停止
};
```

这表示连接不应被复用。

## 消息格式

BenGear 使用统一的消息格式和 `ContentBlock`：

```cpp
struct ContentBlock {
    std::optional<container::String> text;
    std::optional<Json> data;
    container::String type;      // "text" | "tool_use" | "tool_result"
    container::String tool_call_id;
    container::String tool_name;

    static ContentBlock tool_use_block(const ToolCallRequest& req);
};

struct Message {
    MessageRole role;    // system, user, assistant, tool
    container::String content;
    container::Vector<ContentBlock> blocks;
};
```

这允许单个助手消息同时包含文本内容和多个工具调用。

## 重试模型

提供商客户端使用重试助手包装 LLM 请求：

```cpp
// 异步重试（OpenAI/Anthropic 共享）
auto result = co_await with_retry_async(loop, settings, "operation", [&] {
    return provider_.chat_async(...);
});

// 异步 HTTP 重试（重试原始 HTTP 请求，成功后应用转换）
auto result = co_await with_http_retry_async(loop, settings, "operation",
    [&] { return http_post_async(...); },
    [](auto&& resp) { return parse(resp); }
);
```

重试策略由 `llm_request_retry` 配置。重试助手与提供商无关，只要求结果类型暴露 `status` 字段。

## 扩展指南

添加新提供商：

1. 添加提供商枚举值
2. 添加协议特定的客户端
3. 添加请求体和请求头构建器
4. 如果支持流式，添加流式解析器
5. 扩展 `ProviderClient` 分发
6. 添加请求体形状、请求头、端点补全和流式解析的测试

不要在 `Agent` 中添加提供商特定的协议分支。
