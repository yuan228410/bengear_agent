# 回调设计

## 目的

回调机制用于暴露 Agent 运行时事件，而不将 Agent 耦合到特定的前端。

当前的终端渲染只是一个实现。同样的接口可以被 GUI、Web 服务器、追踪后端或测试框架复用。

## 接口

`AgentCallbacks` 提供以下接口：

```cpp
class AgentCallbacks {
public:
    virtual ~AgentCallbacks() = default;
    // LLM 输出
    virtual void on_token(std::string_view token) const {}
    virtual void on_thinking(std::string_view token) const {}
    virtual void on_tool_call(const ToolCallRequest& call) const {}
    virtual void on_tool_result(const ToolCallResult& result) const {}
    // 计划模式
    virtual void on_plan_detected(const Vector<PlanStep>& steps) const {}
    virtual void on_plan_mode_entered() const {}
    virtual void on_plan_mode_exited() const {}
    virtual void on_step_started(const PlanStep& step, int total) const {}
    virtual void on_step_completed(const PlanStep& step) const {}
    virtual void on_step_skipped(const PlanStep& step) const {}
    virtual void on_plan_completed() const {}
};
```

此外，`NullAgentCallbacks` 提供了空实现，用于不需要回调的场景。

## 事件类型

### 思考（Thinking）

`on_thinking` 接收从提供商流式响应中解析的推理/思考增量。

提供商特定的解析器将字段标准化到此回调：

- OpenAI 兼容：`reasoning_content`、`thinking`
- Anthropic 兼容：`thinking`、`thinking_delta`

### Token

`on_token` 接收最终答案文本 token。空 token 字符串表示思考部分结束（用于从 `[thinking]` 过渡到正常输出）。

### 工具调用

`on_tool_call` 在本地工具执行前触发。包含工具调用 ID、名称和解析后的参数。

### 工具结果

`on_tool_result` 在工具执行后触发，包含状态（成功/错误）、工具调用 ID、名称和输出大小。

### 计划模式

计划模式回调用于步骤化执行的事件通知，所有回调传递结构化数据（`PlanStep`），不含格式化码或 ANSI 转义。

| 回调 | 触发时机 |
|------|---------|
| `on_plan_detected(steps)` | LLM 输出 `## Plan`（自动规划或计划模式） |
| `on_plan_mode_entered()` | 用户输入 `/plan` 进入计划模式 |
| `on_plan_mode_exited()` | 退出计划模式（`/cancel` 或 `/plan off`） |
| `on_step_started(step, total)` | 步骤开始执行 |
| `on_step_completed(step)` | 步骤执行完成 |
| `on_step_skipped(step)` | 步骤被跳过（`/skip`） |
| `on_plan_completed()` | 所有步骤执行完毕 |

## 终端渲染器

CLI 使用 `Renderer` 接口 + `CliApp` 封装：

```cpp
// 一行创建
auto cli_app = ben_gear::cli::CliApp::create(display_config);
auto result = agent.run_session_async(loop, session, prompt, cli_app->callbacks());
```

### Renderer 接口

```cpp
class Renderer {
public:
    virtual void on_response_start() = 0;
    virtual void on_response_end() = 0;
    virtual void on_assistant_text(std::string_view token) = 0;
    virtual void on_thinking(std::string_view token) = 0;
    virtual void on_error(std::string_view message) = 0;
    virtual void on_system(std::string_view message) = 0;
    virtual void on_tool_call(std::string_view id, std::string_view name, std::string_view args_json) = 0;
    virtual void on_tool_result(std::string_view id, std::string_view name, bool success,
                                std::string_view output, size_t output_size) = 0;
    // 计划模式
    virtual void on_plan_steps(std::string_view steps_text) = 0;
    virtual void on_step_started(int step_index, int total, std::string_view description) = 0;
    virtual void on_step_completed(int step_index, std::string_view result) = 0;
    virtual void on_step_skipped(int step_index, std::string_view description) = 0;
    virtual void on_plan_finished() = 0;
    virtual void on_plan_message(std::string_view message) = 0;
};
```

### 终端渲染效果

```text
💭 thinking
  思考内容...

┌ ⚡ read_file
│ {"path":"/tmp/example.txt"}
└ ✓ ok  123B

正文回复（Markdown 实时渲染）...
```

### 模块结构

- `bengear_cli` 库：零 Agent 依赖，独立可复用
  - `Renderer` — 纯虚拟接口
  - `TerminalRenderer` — 终端富文本实现
  - `SilentRenderer` — 静默实现
  - `MarkdownRenderer` — 流式 Markdown 渲染（ANSI 重绘方案，含 Emoji 宽度感知）
  - `Theme` — Dracula 风格主题（暗色+亮色）
  - `TerminalCapabilities` — 终端能力检测
  - `SyntaxHighlighter` — 语法高亮（10+ 语言预编译正则）
  - `DisplayConfig` — 显示配置（可从 JSON 加载，支持 `--md-raw` 覆盖）
  - `Spinner` — 异步等待动画
- `bengear_cli_app` 库：Agent ↔ Renderer 桥接
  - `CliApp` — 封装创建 + 回调适配（内部 RichAgentCallbacks 不暴露）

> **注意**：消息和内容块类型（`ACPMessage`、`ContentBlock`）已统一到 ACP 模块（`acp/core/`），不再在 `llm/message.hpp` 中定义。

### 设计要点

- 参数全部 `string_view`，零 DTO 耦合
- 工厂函数创建，调用者不接触具体类型
- thinking/工具调用/Markdown 渲染均通过 DisplayConfig 配置

## 流式集成

在流式模式下，回调通过 `StreamHandlers` 增量接收数据：

```cpp
struct StreamHandlers {
    StreamTokenHandler on_token;
    StreamThinkingHandler on_thinking;
    StreamToolCallHandler on_tool_call;  // StreamToolCallDelta
    StreamStopHandler on_stop;           // StreamStopInfo
};
```

`StreamToolCallDelta` 提供增量工具调用解析：
- `index` — 工具调用索引（用于多工具响应）
- `id` — 工具调用 ID（首次增量）
- `name` — 工具名称（首次增量）
- `arguments` — 参数片段（跨增量累积）

`StreamStopInfo` 表示流完成：
- `stop_reason` — "end_turn"、"tool_use"、"stop"

解析器内部还跟踪 `StreamStopReason`：
- `none` — 流仍在进行
- `done` — 收到 [DONE] 标记
- `finish_stop` — finish_reason: stop
- `finish_tools` — finish_reason: tool_calls
- `error` — 解析错误

## const 正确性

所有回调方法都是 `const`，允许通过 const 引用传递回调：

```cpp
net::Task<ChatResult> run_session_async(net::EventLoop& loop,
                                        workspace::Session& session,
                                        container::String prompt,
                                        const AgentCallbacks& callbacks);
```

## 扩展指南

### 添加新渲染器

1. 继承 `Renderer` 接口
2. 实现 `on_assistant_text`/`on_thinking`/`on_tool_call` 等方法
3. 通过工厂函数创建

```cpp
class WebRenderer : public Renderer { ... };
std::unique_ptr<Renderer> create_web_renderer() { return std::make_unique<WebRenderer>(); }
```

### 添加新前端

1. 继承 `Renderer`（或直接使用 `AgentCallbacks`）
2. 将渲染或序列化逻辑保留在该子类中
3. 将其传递给 `Agent::run_session_async`
4. 避免在 `Agent` 中添加前端特定的分支

## 设计原则

回调仅用于事件传递。它们不应执行提供商协议解析、工具执行或重试决策。
