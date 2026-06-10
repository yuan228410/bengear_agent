# LLM 协议设计

## ACP 统一协议层

消息和内容块类型已统一到 ACP（Agent Communication Protocol）模块：

```cpp
#include "ben_gear/acp/acp.hpp"
#include "ben_gear/acp/core/message.hpp"       // ACPMessage
#include "ben_gear/acp/core/content_block.hpp" // ContentBlock
```

- `ACPMessage` — 统一消息（替代原 `llm/message.hpp` 中的 `Message`）
- `ContentBlock` — 内容块（text / tool_use / tool_result）
- `ACP` 编解码和流式处理位于 `acp/codec/` 和 `acp/stream/`
- 提供商格式转换通过 `llm/adapter.hpp` 中的 `OpenAIAdapter` / `AnthropicAdapter` 实现

LLM 层使用 ACP 类型作为内部表示，仅在发送请求/解析响应时通过适配器转换为提供商格式。

## 提供商边界

`ProviderClient` 是协议分发边界。它检查配置的提供商并委托给协议特定的客户端：

- `OpenAiClient`
- `AnthropicClient`

适配器（`llm/adapter.hpp`）负责 ACP ↔ 提供商格式转换：

- `OpenAIAdapter::to_openai_format()` / `from_openai_format()`
- `AnthropicAdapter::to_anthropic_format()` / `from_anthropic_format()`

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

BenGear 使用 ACP 统一的消息格式和 `ContentBlock`（定义在 `acp/core/`）：

```cpp
// acp/core/content_block.hpp
struct ContentBlock {
    std::optional<container::String> text;
    std::optional<Json> data;
    container::String type;      // "text" | "tool_use" | "tool_result"
    container::String tool_call_id;
    container::String tool_name;

    static ContentBlock tool_use_block(const ToolCallRequest& req);
};

// acp/core/message.hpp
struct ACPMessage {
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

## 用量统计与延迟追踪

ProviderClient 自动为每次 LLM 请求采集 token 用量和延迟，全链路可追踪。

### 数据结构

```cpp
// 单次请求 token 用量（从 API 响应 usage 字段提取）
struct TokenUsage {
    int prompt_tokens = 0;      // 输入 token
    int completion_tokens = 0;  // 输出 token
    int total_tokens = 0;       // 总计
    int cached_tokens = 0;      // 缓存命中（OpenAI cached_prompt_tokens / Anthropic cache_read_input_tokens）
};

// 单次请求延迟
struct RequestLatency {
    double total_seconds = 0.0;  // 请求总耗时
    double ttfb_seconds = 0.0;   // 首 token 延迟（流式有效）
    bool has_ttfb = false;       // 是否有 TTFB 数据
};
```

### 采集链路

```
OpenAI/Anthropic 响应 → Parser 提取 usage → StreamHandlers.usage_out
                                                    ↓
                                              ProviderClient 记录
                                                    ↓
                                            UsageTracker（线程安全累计）
                                                    ↓
                                           AgentCallbacks::on_response_stats
                                                    ↓
                                            Renderer::on_usage_stats（终端 dim 行）
```

### 流式 usage 提取

- OpenAI：请求体添加 `stream_options: {include_usage: true}`，最后一个 SSE chunk 包含 usage
- Anthropic：`message_start` 事件含 `input_tokens`，`message_delta` 事件含 `output_tokens`

### TTFB 捕获

`TtfbCapture` 封装了流式 TTFB 捕获逻辑，包装 `on_token` 回调：

```cpp
TtfbCapture ttfb;
handlers.on_token = ttfb.wrap(std::move(handlers.on_token));
// ... co_await request ...
auto latency = ttfb.build_latency(start);  // 自动计算 total + ttfb
```

线程安全（`atomic<bool>`），独立文件避免循环依赖。

### 终端展示

响应完成后，终端自动显示 dim 行（流式 + 非流式均展示）：

```
──── ↑9891 ↓17  1.23s (ttfb 0.45s)
```

- `────` — 4 个 box-drawing 横线（非 unicode 终端 fallback `----`）
- `↑N` — 输入 token 数
- `↓N` — 输出 token 数（含 thinking + 工具调用参数）
- `Xs` — 请求总延迟
- `(ttfb Xs)` — 首 token 延迟（仅流式有）
- 无 ttfb 时：`──── ↑100 ↓50  2.30s`
- 全部 dim 色，不干扰正文阅读

### 压缩判断校准

`UsageTracker::last_actual_prompt_tokens()` 提供上次 API 返回的实际 prompt_tokens，
压缩判断使用 `actual + estimated_increment` 代替纯估算，更精确。

## 上下文溢出检测与恢复

### 检测

`detect_context_overflow()` 在 ProviderClient 统一检测，仅 status==400 时查 body，正常路径零开销：

```cpp
// provider_error.hpp
inline bool detect_context_overflow(int status, std::string_view body) {
    if (status != 400) return false;
    return body.find("context_length") != std::string_view::npos;
}
```

检测后标记到结果结构：
- `ChatResult::is_context_overflow` — 非流式路径
- `StreamResult::is_context_overflow` — 流式路径

### 恢复流程

Agent 层检测到 overflow 后调用 `Session::force_compact()`，内部循环 L0→L4：

1. 调整裁剪参数（本地零开销）
2. 估算 token — 低于 70% 安全线则成功，重试请求
3. 仍超限 → 执行压缩（LLM 摘要）→ 再估算
4. 仍超限 → 升级到下一级（更激进的裁剪+压缩）→ 回到步骤 1
5. L4 后仍超限 → 返回失败

LLM 压缩调用最多 5 次（`max_compact_calls` 参数）。

## 扩展指南

添加新提供商：

1. 添加提供商枚举值
2. 添加协议特定的客户端
3. 添加请求体和请求头构建器
4. 如果支持流式，添加流式解析器
5. 扩展 `ProviderClient` 分发
6. 添加请求体形状、请求头、端点补全和流式解析的测试

不要在 `Agent` 中添加提供商特定的协议分支。
