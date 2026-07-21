# 架构设计

## 设计原则

### 高内聚
- 每个模块职责单一
- 相关功能聚合在一起
- 模块内部高度相关

### 低耦合
- 模块间通过接口交互
- **ServiceRegistry** 统一管理服务引用，通过 `services().resolve<T>()` 获取，消除直接依赖
- **EventBus** 发布/订阅解耦事件生成和消费，替代旧回调链
- 易于单元测试和替换

### 统一抽象
- 一套代码支持多个 LLM 提供商
- 统一的消息格式（ContentBlock）
- 统一的工具调用流程

### 可扩展
- 易于添加新工具
- 易于支持新 LLM 提供商
- 插件化架构

> 生命周期和所有权规则详见：[Ownership and Lifecycle Rules](ownership.md)。涉及 `Runtime`、`WorkflowEngine`、`agent::core::Agent` 或 `ToolRegistry` 闭包的改动，应同步检查该文档约束。

## 核心架构

### 三层 Agent 架构

BenGear 采用三层 Agent 架构，将运行时、执行层和插件系统分离：

```
┌─────────────────────────────────────────────────────┐
│  agent::plugin（插件系统）                            │
│  动态库加载（.dll/.so），标准 ABI 约定                │
├─────────────────────────────────────────────────────┤
│  agent::runtime::Runtime（完整运行时）                │
│  ServiceRegistry 管理全部子服务，services().resolve<T>() 访问 │
│  生命周期由 LifecycleManager 管理                      │
├─────────────────────────────────────────────────────┤
│  agent::execution（执行原语层）                       │
│  ExecutionLoop + IInterceptor 链                     │
│  模式无关的 ReAct 循环                               │
└─────────────────────────────────────────────────────┘
```

### ServiceRegistry — 类型安全服务注册表

所有子服务通过 `ServiceRegistry` 统一管理，通过 `services().resolve<T>()` 类型安全访问：

```cpp
class Runtime : public std::enable_shared_from_this<Runtime> {
    friend class RuntimeFactory;
public:
    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    /// 服务注册表（获取所有子服务的统一入口）
    base::ServiceRegistry& services() noexcept { return services_; }
    const base::ServiceRegistry& services() const noexcept { return services_; }

    /// 优雅关闭全部服务
    void shutdown();

    /// 生命周期状态
    LifecycleManager& lifecycle() noexcept;
    const LifecycleManager& lifecycle() const noexcept;

    /// 在会话上运行 Agent 循环
    struct SessionRunConfig {
        net::EventLoop& loop;
        workspace::Session& session;
        std::string prompt;
        net::CancellationToken cancel;
    };

    net::Task<llm::ChatResult> run_session_async(SessionRunConfig config);
    std::unique_ptr<workspace::Session> make_session(std::string session_id = {});

private:
    Runtime(config::Settings settings,
            workspace::WorkspaceContext ws_ctx);
    void register_services();

    base::ServiceRegistry services_;
    config::Settings settings_;
    workspace::WorkspaceContext ws_ctx_;
    LifecycleManager lifecycle_;
    base::EventBus event_bus_;
    base::NoopMetricsCollector metrics_;
    base::NoopTracer tracer_;
    // 其他成员仅由 RuntimeFactory 初始化，通过 ServiceRegistry 访问
    struct InternalServices;
    std::unique_ptr<InternalServices> internal_;
};
```

**关键变更**：
- **无直接 accessor**：旧 Runtime 的 `provider()`、`tools()`、`workflow_engine()`、`sub_agent_runtime()` 等 40+ 访问器全部移除，改为 `services().resolve<T>()` 统一获取
- **工具注册**：`register_tool()` 已从 Runtime 移除，通过 `services().resolve<ToolRegistry>()->register_tool()` 调用
- **EventBus 内置**：Runtime 内置 `EventBus` 实例，所有 Agent 事件通过 `event_bus.publish<T>()` 发布

**服务访问示例**：
```cpp
auto rt = RuntimeFactory::create(settings, ws_ctx);
auto& svc = rt->services();

// 旧: rt->provider()
auto& provider = *svc.resolve<llm::ProviderClient>();

// 旧: rt->workflow_engine()
auto& wf = *svc.resolve<workflow::WorkflowEngine>();

// 旧: rt->tools()
auto& tools = *svc.resolve<capabilities::tool::ToolRegistry>();

// 工具注册
svc.resolve<capabilities::tool::ToolRegistry>()->register_tool(...);
```

**初始化流程**（`RuntimeFactory::create()` 内部调用 5 阶段初始化）：
1. `init_infrastructure()` — HTTP 工作流注册 + WorkspaceManager 创建
2. `init_memory_system()` — MemoryStore + ContextBuilder + HistoryDB
3. `init_tool_system()` — 工具注册 + 技能发现 + MCP 连接
4. `init_orchestration()` — WorkflowEngine + SubAgentRuntime + 插件加载
5. `inject_agent_defaults()` — 注入默认服务实现

**生命周期状态机**：`LifecycleManager` 负责 Runtime 的状态转换（`Uninitialized → Initialized → Running → Shutdown`），`Runtime::shutdown()` 触发优雅关闭。

**典型用法**：
```cpp
auto runtime = agent::runtime::RuntimeFactory::create(settings, ws_ctx);
auto session = runtime->make_session("my-session");

auto result = net::sync_wait(io_loop,
    runtime->run_session_async({io_loop, *session, "prompt", cancel}));
// 注意：SessionRunConfig 无 event_sink 字段 — 事件通过 EventBus 传递
```

### EventBus — 类型安全事件总线

Agent 事件通过 `EventBus` 发布/订阅模式解耦，替代旧 `AgentEventSinks` 回调聚合体：

```cpp
// 事件类型定义（位于 agent/core/events.hpp）
namespace ben_gear::agent {
struct TokenEvent { std::string_view token; };
struct ThinkingEvent { std::string_view token; };
struct ToolCallEvent { const acp::ToolCallRequest& call; };
struct ToolResultEvent { const acp::ToolCallResult& result; };
struct ResponseStatsEvent { ... };
struct ExecutionPlanEvent { ... };
struct TodoUpdateEvent { ... };
struct SubAgentStartEvent { ... };
struct SubAgentProgressEvent { ... };
struct SubAgentCompleteEvent { ... };
struct SubAgentErrorEvent { ... };
}
```

**消费方订阅**：
```cpp
auto sub = event_bus.subscribe<TokenEvent>([](const TokenEvent& e) {
    renderer.on_token(e.token);
});
// Subscription RAII：析构时自动取消订阅
```

**生产者发布**：
```cpp
event_bus.publish(TokenEvent{token});
event_bus.publish(ToolCallEvent{call});
```

**关键设计**：
- 事件类型为普通 struct（无虚函数），按 `std::type_index` 分发
- `Subscription` RAII 令牌，析构自动取消订阅
- `EventBridge` 订阅 EventBus 并将事件转换为 WebSocket 消息（Server 模式）
- `CliApp` 订阅 EventBus 并将事件渲染到终端（CLI 模式）
- SubAgent 通过 EventBus 推送流式进度事件，不再依赖回调接口
- `SessionRunConfig` 已去除 `event_sink` 字段

### 事件接口（AgentEventSinks）—— 向后兼容

`agent::AgentEventSinks` 聚合结构体仍然存在（`agent/core/event_sink.hpp`），用于需要显式回调注入的场景：

```cpp
struct AgentEventSinks {
    StreamEventSink& stream;
    ToolEventSink& tool;
    OrchestrationEventSink& orch;
    SubAgentEventSink& sub_agent;
};
```

每个接口都有 Null 实现：`NullStreamSink`、`NullToolSink`、`NullOrchestrationSink`、`NullSubAgentEventSink`。

> 现有代码优先使用 EventBus。AgentEventSinks 保留用于与旧代码兼容。

### IoContext 统一 I/O 管理

`IoContext` 封装 EventLoop + 专属线程，提供三个独立 I/O 上下文：

```cpp
class IoContext {
public:
    explicit IoContext(std::string_view name);
    ~IoContext();  // drain(timeout) + stop thread

    EventLoop& loop() noexcept;
    void drain(int timeout_ms = 30000);  // 超时保护
};
```

| 上下文 | 用途 |
|--------|------|
| `io` | LLM HTTP 请求、流式响应 |
| `workflow` | 工作流任务调度 + 子 Agent 执行 |
| `util` | 记忆更新、轻量级任务 |

所有 IoContext 通过 `services().resolve<net::IoContext>()` 获取。

### Session 隔离

每个 Session 独占以下资源，无需加锁：

- `ConversationHistory` — 对话历史（位于 llm 模块）

EventLoop 由 IoContext 全局管理（io / workflow / util 三个上下文），Session 通过参数传入引用，不再持有。

```cpp
class Session {
public:
    explicit Session(SessionConfig config, SessionDeps deps, llm::ToolRegistry& tools);

    llm::ConversationHistory& history();        // 独占

    // 压缩器访问（供 CompactionInterceptor 使用）
    memory::Compactor* compactor();
    memory::MemoryUpdater* memory_updater();

    // 压缩检查（手动触发，如 /compact 命令；自动压缩由 CompactionInterceptor 处理）
    void maybe_compact(EventLoop& loop, const ProviderClient& provider, const ToolRegistry& tools);
    void persist_message(role, content, HistoryDB& db);
    void persist_assistant_with_tools(content, tool_calls, HistoryDB& db);
    void persist_tool_result(tool_call_id, tool_name, content, HistoryDB& db);
    void restore_from_db(HistoryDB& db);
};
```

## 核心模块

### 1. Agent 层 (`src/agent/`)

**职责**：Agent 运行时 — ServiceRegistry 统一管理全部服务 + ExecutionLoop 执行原语

**核心组件**：

| 层级 | 类 | 职责 |
|------|------|------|
| 运行时 | `agent::runtime::Runtime` | 轻量级服务编排器，ServiceRegistry 管理全部子服务 |
| 运行时 | `agent::runtime::RuntimeFactory` | Runtime 创建和 5 阶段初始化 |
| 运行时 | `agent::runtime::LifecycleManager` | 生命周期状态机（Uninitialized→Initialized→Running→Shutdown） |
| 服务 | `base::ServiceRegistry` | 类型安全服务注册表，通过 `resolve<T>()` 获取服务 |
| 事件 | `base::EventBus` | 类型安全事件总线，`publish<T>()` / `subscribe<T>()` |
| 执行 | `agent::execution::ExecutionLoop` | ReAct 原语（纯循环，不含模式逻辑） |
| 执行 | `agent::execution::IInterceptor` | 拦截器接口（before_llm / before_tools / after_tools / should_stop） |
| 执行 | `agent::execution::PlanInterceptor` | 计划模式拦截器：审核期间过滤写操作、终态停止 |
| 执行 | `agent::execution::CompactionInterceptor` | 上下文压缩拦截器：软压缩 + 溢出恢复 |
| 执行 | `agent::execution::IExecutionLoopServices` | ExecutionLoop 与 LLM Provider 解耦接口 |
| 执行 | `agent::execution::IToolTimeoutPolicy` | 工具超时可配置策略 |
| 事件 | `agent::TokenEvent / ToolCallEvent / ...` | EventBus 事件类型（普通 struct，无虚函数） |
| 事件 | `agent::StreamEventSink` | 流式输出事件接口（ISP，向后兼容） |
| 事件 | `agent::ToolEventSink` | 工具调用事件接口（ISP，向后兼容） |
| 事件 | `agent::OrchestrationEventSink` | 编排事件接口（ISP，向后兼容） |
| 配置 | `agent::SubAgentConfig` | 子 Agent 配置（`src/agent/core/`） |
| 上下文 | `agent::runtime::IToolContext` | 工具子系统抽象接口（注入/测试） |
| 上下文 | `agent::runtime::IMemoryContext` | 记忆子系统抽象接口 |
| 上下文 | `agent::runtime::IOrchestrationContext` | 编排子系统抽象接口 |

**关键接口**：
- `base::ServiceRegistry` — 类型安全服务注册，通过 `services().resolve<T>()` 获取全部子服务
- `base::EventBus` — 发布/订阅，Agent 事件解耦传递
- `base::IMetricsCollector` — 可观测性指标采集
- `base::ITracer` — 分布式追踪
- `IToolContext` / `IMemoryContext` / `IOrchestrationContext` — 子系统门面接口（可模拟注入）

**典型用法**：
```cpp
auto runtime = agent::runtime::RuntimeFactory::create(settings, ws_ctx);
auto session = runtime->make_session("my-session");

// 获取服务：services().resolve<T>()
auto& provider = *runtime->services().resolve<llm::ProviderClient>();
auto& tools = *runtime->services().resolve<capabilities::tool::ToolRegistry>();

// 工具注册
tools.register_tool("my_tool", "Description", parameters, executor);

// 运行会话（事件通过 EventBus 传递，无需显式传入 sink）
auto result = net::sync_wait(io_loop,
    runtime->run_session_async({io_loop, *session, "prompt", cancel}));
```

### 1.5 子 Agent 系统 (`agent::runtime::SubAgentRuntime`)

**职责**：主 Agent 通过 LLM tool call（`delegate_task` / `delegate_tasks`）自动委派任务给子 Agent

**分层架构**：
```
┌─────────────────────────────────────────────────────┐
│  UI 层（CLI / Web / API）                            │
│  订阅 EventBus 或实现 AgentEventSinks                 │
├─────────────────────────────────────────────────────┤
│  事件总线 — EventBus 发布/订阅                        │
│  TokenEvent / ToolCallEvent / SubAgentStartEvent …   │
│  纯数据 struct，零 UI 依赖                            │
├─────────────────────────────────────────────────────┤
│  编排层 — agent::runtime::SubAgentRuntime             │
│  调度 / 生命周期 / 并行 / 取消 / 监控 / 聚合          │
├─────────────────────────────────────────────────────┤
│  Runtime 层 — Runtime + Session + ToolRegistry       │
│  run_session_async({loop, session, prompt, cancel})   │
├─────────────────────────────────────────────────────┤
│  基础层 — Runtime / EventLoop / ThreadPool           │
└─────────────────────────────────────────────────────┘
```

**核心类**：
- `agent::runtime::SubAgentRuntime` — 子 Agent 运行时，管理生命周期、并行执行、聚合摘要（独立类，不再嵌套于 Runtime）
- `SubAgentEvent` — 结构化事件（`std::variant` payload），UI 无关
- `SubAgentResult` — 子 Agent 执行结果（含 usage、latency、artifacts），`execute()` 接受 `SubAgentTask` 返回 `SubAgentResult`
- `SubAgentTask` — 任务描述（prompt、tool_filter、timeout 等），定义于 `agent/sub_agent_types.hpp`
- `agent::SubAgentConfig` — 配置（max_parallel、default_timeout、auto_summary 等），位于 `src/agent/core/sub_agent_config.hpp`

**执行拓扑**：
```
主 Agent (main EventLoop)
  ├── LLM → delegate_tasks 工具调用
  ├── tool_manager_.execute_tools()
  │     └── thread pool worker
  │           └── sync_wait(wf_loop, ...)
  └─────────────────┼───────────────────
                    ▼
          wf_context EventLoop (独立线程)
          ┌──────────┐ ┌──────────┐ ┌──────────┐
          │ 子Agent 1 │ │ 子Agent 2 │ │ 子Agent 3 │
          │ Session  │ │ Session  │ │ Session  │
          │ Filtered │ │ Filtered │ │ Filtered │
          │ Registry │ │ Registry │ │ Registry │
          └──────────┘ └──────────┘ └──────────┘
          共享: ProviderClient / ThreadPool / HttpClient
```

**关键设计决策**：
- 子 Agent 在 `wf_context` EventLoop 上执行，避免主 EventLoop 死锁
- `create_filtered_registry()` 自动排除 `delegate_task`/`delegate_tasks`，禁止递归委派
- 会话持久化：子 Agent 会话通过 `session_type=sub_agent` + `parent_id` 关联主会话
- 输出控制：超长输出自动截断或 LLM 摘要，保护主 Agent 上下文
- **事件驱动**：SubAgent 通过 EventBus 推送流式进度事件（`SubAgentStartEvent`、`SubAgentProgressEvent`、`SubAgentCompleteEvent`、`SubAgentErrorEvent`），消费方订阅即可，不再依赖回调接口
- **上下文组装**：子 Agent 通过 `ContextBuilder` 管道，使用 `PromptSection::sub_agent` 预设 + `PromptMode::sub_agent` 注入最小化系统提示

### `Services().resolve<T>()` 服务一览

以下是通过 `services().resolve<T>()` 可获取的主要服务：

| 服务类型 | ServiceRegistry 注册名 | 说明 |
|---------|----------------------|------|
| `config::Settings` | `services().resolve<config::Settings>()` | 配置设置 |
| `llm::ProviderClient` | `services().resolve<llm::ProviderClient>()` | LLM Provider |
| `capabilities::tool::ToolRegistry` | `services().resolve<capabilities::tool::ToolRegistry>()` | 工具注册表 |
| `memory::MemoryStore` | `services().resolve<memory::MemoryStore>()` | 记忆存储 |
| `workspace::WorkspaceManager` | `services().resolve<workspace::WorkspaceManager>()` | 工作空间管理 |
| `workspace::HistoryDB` | `services().resolve<workspace::HistoryDB>()` | 历史数据库 |
| `workflow::WorkflowEngine` | `services().resolve<workflow::WorkflowEngine>()` | 工作流引擎 |
| `mcp::MCPManager` | `services().resolve<mcp::MCPManager>()` | MCP 管理器 |
| `base::EventBus` | `services().resolve<base::EventBus>()` | 事件总线 |
| `base::concurrency::ThreadPool` | `services().resolve<base::concurrency::ThreadPool>()` | 线程池 |
| `IToolContext` | `services().resolve<IToolContext>()` | 工具上下文 |
| `IMemoryContext` | `services().resolve<IMemoryContext>()` | 记忆上下文 |
| `IOrchestrationContext` | `services().resolve<IOrchestrationContext>()` | 编排上下文 |
| `net::IoContext` (io) | `services().resolve<net::IoContext>()` | I/O 事件循环 |

### 1.6 计划模式 (`orchestration::PlanManager`)

**职责**：支持 Agent 先规划后执行的受控工作流

**状态机**：

```
idle → drafting → reviewing → confirmed → executing
        ↑                                    │
        └────────── /cancel ←───────────────┘
```

| 状态 | 触发 | 行为 |
|------|------|------|
| `idle` | 初始状态 | 正常聊天，无计划约束 |
| `drafting` | `/plan <描述>` | LLM 生成计划草案（prompt 约束：只做规划，不执行） |
| `reviewing` | 计划生成完成 | 用户审阅，可 `/approve` 或 `/cancel` |
| `confirmed` | `/approve` | 计划确认，等待执行 |
| `executing` | 状态自动切换 | 按计划逐步执行 |

**CLI 命令**：
- `/plan <描述>` — 触发计划生成，进入 reviewing 状态
- `/approve` — 批准计划，进入 executing 状态
- `/cancel` — 取消计划，回到 idle 状态

|**关键设计**：
- **基于 Prompt**：不通过工具过滤控制行为，所有工具可用；行为约束完全由系统提示注入实现
- `PromptMode::plan_reviewing` — 注入 "DO NOT implement, only plan. Output a structured plan only." 指令
- `PromptMode::plan_executing` — 注入 "Follow the approved plan step by step. Execute without deviation." 指令
- `Runtime::run_session_async` 在调用 `ContextBuilder` 前通过 `PlanManager` 检测状态，设置对应的 `PromptMode`
- 与 `ExecutionLoop` 解耦：计划模式状态由 `PlanManager` 管理，`ExecutionLoop` 只关心 ReAct 循环本身
- **注意**：`PlanManager` 通过 `services().resolve<IOrchestrationContext>()->plans()` 获取，非旧 Runtime 直接成员

### 2. CLI 渲染层 (`src/cli/`)

**职责**：终端富文本渲染，零 Agent 依赖

**两个库**：
- `bengear_cli_render` — 独立可复用渲染库（Renderer/Theme/Markdown/Highlight/Spinner/DisplayConfig）
- `bengear_cli_app` — Agent ↔ Renderer 桥接（CliApp：创建 Renderer + 订阅 EventBus）

**核心接口**：
```cpp
class Renderer {
    virtual void on_response_start() = 0;
    virtual void on_response_end() = 0;
    virtual void on_assistant_text(std::string_view token) = 0;
    virtual void on_thinking(std::string_view token) = 0;
    virtual void on_error(std::string_view message) = 0;
    virtual void on_tool_call(...) = 0;
    virtual void on_tool_result(...) = 0;
};
```

**EventBus 连接**：CLI 模式下通过 `cli_app->connect_to_event_bus(event_bus)` 订阅 EventBus，接收 TokenEvent/ToolCallEvent 等事件并渲染到终端。
```

**Markdown 流式渲染**：ANSI 重绘方案 — 每个 token 即时输出原始文本，遇 `\n` 时 `clear_line + \r` 重绘为带样式的 Markdown。

**关键功能**：
- Session-based 对话管理（每个 Session 独占 history）
- 流式/非流式双路径（根据 `settings.stream` 自动选择）
- 流式增量工具调用解析（`StreamToolCallDelta` → `PendingToolCall`）
- 工具调用循环（`max_tool_steps` 轮次限制，`max_tool_calls` 累计限制，`max_tool_calls_per_step` 单轮限制）
- 记忆压缩（Compactor）
- LLM 记忆更新（MemoryUpdater）
- 持久化到 HistoryDB

### 3. LLM 层 (`src/llm/`)

**职责**：LLM 协议实现和工具调用

**核心模块**：

#### 客户端
- `provider_interface.hpp` — `IProviderClient` 纯虚接口（`OpenAiClient` / `AnthropicClient` 均实现此接口）
- `provider_client.hpp` — `ProviderClient` 门面（聚合 `IProviderClient` + 故障转移 + 用量追踪）
- `provider_registry.hpp` — Provider 注册表（单例 + 静态 registrar，消除硬编码分发）
- `openai_client.hpp` — OpenAI 客户端（实现 `IProviderClient`）
- `anthropic_client.hpp` — Anthropic 客户端（实现 `IProviderClient`）

#### 消息
- `message.hpp` — 统一消息格式 + ContentBlock（text/tool_use/tool_result）
- `chat.hpp` — 聊天请求/响应
- `stream.hpp` — 流式响应处理器（StreamHandlers + StreamToolCallDelta）

#### 重试
- `retry.hpp` — 统一异步重试（`with_retry_async`、`with_http_retry_async`）

#### 内部实现
- `internal/openai_parser.hpp` — OpenAI 流解析器
- `internal/anthropic_parser.hpp` — Anthropic 流解析器
- `internal/sse.hpp` — SSE 解析

**关键功能**：
- 原生工具调用 API
- 流式响应解析（含增量工具调用）
- 协议适配
- 统一异步重试
- ProviderRegistry 单例 + 静态 registrar，消除 provider_client.cpp 中硬编码 if/else
- 故障转移（冷却追踪 + 指数退避 + 30s 探针）
- TTFB 捕获 + 用量统计 + 上下文溢出自动恢复（L0→L4 渐进式裁剪+压缩）

### 4. 插件加载器 (`src/plugins/`)

**职责**：动态库加载与自动能力/Provider 注册

**核心类**：
- `PluginLoader` — 扫描目录加载 `.dll`/`.so`，调用 `ben_gear_plugin_init()`
- `ben_gear_plugin_init()` — 插件导出的初始化函数，内部调用 `BEN_GEAR_REGISTER_CAPABILITY` / `BEN_GEAR_REGISTER_PROVIDER`

**插件契约**：
- 导出 `extern "C" void ben_gear_plugin_init();`
- 内部使用 `BEN_GEAR_REGISTER_CAPABILITY` / `BEN_GEAR_REGISTER_PROVIDER` 宏
- 编译为 `.dll` (Windows) / `.so` (Linux/macOS)，放入配置的 `plugins_dir`
- 启动时 `PluginLoader(plugins_dir).load_all()` 自动加载并注册

### 5. 工具层 (`src/capabilities/tool/`)

**职责**：工具注册、管理和执行

**核心类**：
- `ToolRegistry` — 工具注册表（线程安全，shared_mutex）
- `ToolCallManager` — 工具调用管理器

**工具分类**：
- 内置工具：原 `builtin_tools.cpp`（1240 行）已拆分为 8 个文件：`file_tools.cpp`、`shell_tools.cpp`、`http_tools.cpp`、`extended_tools.cpp`、`replace_tools.cpp`、`search_content_tools.cpp`、`env_tools.cpp`、`image_tools.cpp`，位于 `capabilities/tool/`
- 子 Agent 工具：`delegate_task` / `delegate_tasks`，位于 `capabilities/tool/sub_agent_tools.hpp/cpp`
- 技能工具：get_skill、install_skill、remove_skill、enable_skill、disable_skill、list_skills
- 记忆工具：read/write_memory、recall、read/write_soul、read/write_rules、append_episode
- 工作空间工具：list/create/remove/restore_workspace
- MCP 工具：自动发现，`mcp_` 前缀

### 6. 配置层 (`src/config/`)

**职责**：配置加载和管理

**核心功能**：
- JSON 配置解析：原 `loader.cpp`（779 行）中 `apply_json_to_settings` 已拆分为 14 个领域专用解析函数（`parse_llm_settings`、`parse_agent_settings` 等）
- `model_config` 分组格式（provider → models 列表）
- 多层配置合并
- 环境变量替换
- MCP 服务器配置
- 多级管理字段（username、workspace_name、session_id）

### 7. 技能层 (`src/capabilities/skill/`)

**职责**：技能发现、加载和渐进式披露

**核心类**：
- `SkillDefinition` — 技能定义（从 SKILL.md 解析）
- `SkillLoader` — 技能加载器（目录扫描 + 按需加载）

**关键功能**：
- SKILL.md（frontmatter key: value + Markdown）解析
- 全局/项目两级目录扫描
- 渐进式披露（3 级加载）
- `get_skill` 工具注册（Level 2 入口）
- 5 个 LLM 可调用的技能管理工具

### 8. MCP 层 (`src/capabilities/mcp/`)

**职责**：MCP 协议客户端，连接外部工具服务器

**核心类**：
- `MCPClient` — 单个 MCP 服务器连接（stdio + HTTP 双传输）
- `MCPManager` — 多服务器管理和工具路由

**关键功能**：
- stdio 传输（安全子进程通信，fork+execvp / CreateProcess）
- HTTP 传输（JSON-RPC over HTTP POST）
- stdio 读取超时（默认 30s，POSIX 使用 poll()）
- 自动发现 MCP 服务器工具
- 并行工具执行（ThreadPool，同 server 串行，不同 server 并行）


### 9. ACP 协议层 (`src/acp/`)

**职责**：Agent Communication Protocol — 统一的消息协议层，一级模块

**子模块**：
- `acp/core/` — 核心数据结构：`ACPMessage`、`ContentBlock`、`ACPRole`、`ProtocolVersion`（零依赖）
- `acp/types/` — 协议类型：`ToolCallRequest`、`ToolCallResult`（从 `capabilities/tool` 迁入，解耦工具层与协议层）
- `acp/codec/` — 编解码器：`JsonSerializer` / `JsonParser`（JSON 序列化/解析）
- `acp/stream/` — 流式事件处理：`StreamHandler` / `StreamDispatcher`
- `acp/adapter/` — 工具适配器：ACP ↔ 内部工具类型转换

**关键设计**：
- 位于 `src/acp/`，与 `src/agent/`、`src/llm/` 同级，不嵌套在任何子系统下
- 统一 `acp.hpp` 伞头文件聚合全部子模块
- `ToolCallRequest` / `ToolCallResult` 从 `capabilities::tool` 迁入 `acp::` 命名空间，协议类型与工具执行实现解耦

### 10. 记忆系统 (`src/memory/`)

**职责**：三层级记忆存储、上下文压缩和记忆更新

**核心类**：
- `MemoryStore` — 三层级记忆存储（跨进程文件锁 + 原子写入）
- `EpisodeStore` — 每日情景记忆（FileLock 安全追加）
- `ContextBuilder` — 系统提示组装器 + CJK 感知 token 估算
- `Compactor` — 上下文压缩器（软/硬阈值，持久化缓存）
- `MemoryUpdater` — LLM 记忆更新器（重试 + 标签提取）
- `merge_sections()` — 三层级 section 合并算法（last-wins）

### 11. 工作空间 (`src/workspace/`)

**职责**：多用户多工作空间管理

**核心类**：
- `WorkspaceManager` — CRUD + 软删除/恢复 + 默认模板
- `Session` — 会话类（独占 history + Compactor + MemoryUpdater）
- `TierPaths` — 三层级路径集合
- `WorkspaceContext` — 传递给 Agent/Session 的上下文


### 12. 工作流引擎 (`src/workflow/`)

**职责**：DAG 任务编排、并行执行、命名空间隔离

**核心类**：
- `WorkflowEngine` — 工作流引擎（注册/执行/暂停/恢复/取消）
- `WorkflowTemplateLibrary` — 全局只读模板库
- `WorkflowScheduler` — DAG 调度器（拓扑排序 + 并行执行）
- `DAG` — 有向无环图（环检测 + 就绪任务查询）
- `TaskExecutor` — 线程池任务执行器（含重试）
- `LLMTask` / `ToolTask` — 具体任务类型

**三层架构**：

| 层级 | 组件 | 生命周期 |
|------|------|---------|
| 全局层 | `WorkflowTemplateLibrary` | 应用启动 → 退出 |
| Agent 层 | `WorkflowEngine` | Agent 创建 → 销毁 |
| 会话层 | Session 状态映射 | 会话创建 → 销毁 |

**关键功能**：
- 自动命名空间隔离（`username::workspace::session_id` 前缀）
- 5 种任务类型（llm/tool/function/condition/subflow）
- 变量替换（`{{task_id}}` / `{task_id}` / `{{task_id.result}}`）
- 工具级超时覆盖（`execute_workflow` 300s，其他 30s）
- 15 个 LLM 可调用的工作流工具
- 4 个内置模板（code_review/documentation/refactoring/test_generation）

### 13. 网络层 (`src/net/`)

**职责**：网络通信

**核心模块**：
- `http.hpp` — HTTP 客户端（内置连接池 + ObjectPool）
- `connection_pool.hpp` — 连接池（预热 + shared_mutex 读写锁）
- `event_loop.hpp` — 事件循环
- `socket.hpp` — Socket 封装
- `task.hpp` — 协程任务
- `tcp_stream.hpp` — TCP 流

### 14. 日志层 (`src/log/`)

**职责**：异步日志

**核心模块**：
- `logger.hpp` — 日志记录器（前端轻量采集 + 后端异步格式化）
- `sink.hpp` — 输出目标（Stdout / File 轮转 / TCP Server）
- `level.hpp` — 日志级别
- `configure.hpp` — 日志配置

### 15. TLS 抽象层

**职责**：后端无关的 TLS 操作抽象

**核心类**：
- `TlsEngine` — TLS 引擎抽象接口（创建 Session、全局初始化、后端名称）
- `TlsEngine::Session` — TLS 会话（握手、加密读写、优雅关闭）
- `TlsConfig` — TLS 配置（证书验证、SNI、协议版本）

**后端支持**：
- **MbedTLS**（默认，vendor）— 适用于 macOS/Linux
- **OpenSSL**（系统）— 适用于需要系统 OpenSSL 的场景
- **Schannel**（Windows 原生）— 适用于 Windows
- **none** — 禁用 TLS

**关键设计**：
- 编译期通过 CMake `TLS_BACKEND` 选择后端
- 运行时通过 `set_global_tls_engine()` 替换
- `PooledConnection::tls_session` 使用 `unique_ptr<TlsEngine::Session>` 类型安全
- 位于 `src/net/tls/`，属于 `bengear_net` 构建目标

### 16. 压缩抽象层

**职责**：后端无关的压缩/解压操作抽象

**核心类**：
- `CompressEngine` — 压缩引擎抽象接口（inflate/deflate）

**后端支持**：
- **zlib**（默认，vendor）— 通用压缩
- **none** — 禁用压缩

**关键设计**：
- CMake `COMPRESS_BACKEND` 选择后端
- 通过 `global_compress_engine().inflate()` 统一调用
- 位于 `src/compress/`，属于 `bengear_compress` 构建目标

## 工作流程

```text
RuntimeFactory::create(settings, ws_ctx) → Runtime（Services 全部通过 ServiceRegistry 管理）
用户输入
  → Runtime.run_session_async（委托给 agent::execution::ExecutionLoop）
  → ContextBuilder.build() 组装系统提示（PromptSection 位掩码 + PromptMode 注入）
  → Session.history 追加 user 消息
  → ExecutionLoop ReAct 循环:
      → 通过 IExecutionLoopServices 调用 LLM (带工具定义 + IInterceptor 链预处理)
      ├─ 流式：StreamHandlers 增量解析 token + thinking + tool_call → EventBus publish
      └─ 非流式：完整响应解析
    → LLM 返回工具调用请求 → IInterceptor 链后处理
    → ToolManager 执行工具 → EventBus publish tool_call/tool_result
    → 构建工具结果消息
    → 持久化到 HistoryDB
    → Maybe Compact（CompactionInterceptor 软压缩 + 溢出恢复）
      ├─ 软/硬双阈值检测
      ├─ 批量摘要旧轮次
      └─ 持久化缓存
    → Maybe MemoryUpdate（MemoryUpdater 更新长期记忆和情景）
    → 循环下一个 ReAct 轮次
  → 返回最终结果
```

## 系统提示组装

`ContextBuilder` 通过 `PromptSection` 位掩码控制 section 组装，通过 `PromptMode` 枚举注入模式指令：

### PromptSection 位掩码

`build()` 接受 `PromptSection` 组合，按需选择要包含的 section：

| Section | 位 | 内容 |
|---------|----|------|
| `identity` | `1 << 0` | 核心提示（core_prompt_ 或默认 "You are BenGear, an AI agent."） |
| `directives` | `1 << 1` | 效率指令（硬编码行为规范） |
| `skills` | `1 << 2` | 可用技能列表（SkillLoader 元数据） |
| `rules` | `1 << 3` | RULES.md（行为规范，三层级 section 合并） |
| `soul` | `1 << 4` | SOUL.md（个性/使命，三层级 section 合并） |
| `user` | `1 << 5` | USER.md（用户偏好，优先级取第一个非空） |
| `memory` | `1 << 6` | MEMORY.md（长期记忆，三层级 section 合并） |
| `workspace` | `1 << 7` | 工作空间路径 + AGENTS.md（可选） |

常用组合预设：
- `PromptSection::standard` — identity + directives + skills + rules + soul + user + memory + workspace
- `PromptSection::sub_agent` — identity + directives + skills（子 Agent 最小上下文）
- `PromptSection::character` — rules + soul + user（身份定义区段）
### PromptMode 枚举

`build_mode()` 根据当前模式注入对应的系统指令：

| 枚举值 | 注入指令 |
|--------|---------|
| `normal` | 默认 Agent 行为 |
| `plan_reviewing` | "DO NOT implement, only plan. Output a structured plan only." |
| `plan_executing` | "Follow the approved plan step by step. Execute without deviation." |
| `sub_agent` | 子 Agent 专用行为约束 |

### 扩展性

新增模式只需：
1. 在 `PromptMode` 枚举中添加新值
2. 在 `build_mode()` 中添加 `case` 分支注入对应指令
3. 调用方在 `build()` 前设置 `PromptMode`

## 关键设计模式

### 1. 适配器模式

**应用场景**：协议适配

```cpp
// OpenAI 适配器
Json to_openai_format() const;
static acp::ToolCallRequest from_openai(const Json& j);

// Anthropic 适配器
Json to_anthropic_format() const;
static acp::ToolCallRequest from_anthropic(const Json& j);
```

**优势**：统一抽象、易于扩展新协议、隔离协议细节

### 2. 策略模式

**应用场景**：工具执行

```cpp
using ToolExecutor = std::function<std::string(const Json& arguments)>;
registry.register_tool(name, description, parameters, executor);
```

**优势**：工具实现灵活、易于测试、运行时可配置

### 3. 观察者模式

**应用场景**：回调通知

```cpp
// 三个独立接口（ISP），消费方只需实现关心的部分
class MyStreamSink : public agent::StreamEventSink {
    void on_token(std::string_view token) const override;
    void on_thinking(std::string_view token) const override;
    void on_response_stats(...) const override;
};
class MyToolSink : public agent::ToolEventSink {
    void on_tool_call(const acp::ToolCallRequest& call) const override;
    void on_tool_result(const acp::ToolCallResult& result) const override;
    void on_tool_blocked(...) const override;
};
class MyOrchSink : public agent::OrchestrationEventSink {
    void on_execution_event(const orchestration::ExecutionEvent& event) const override;
    void on_todo_update(...) const override;
};
```

|**优势**：解耦事件生成和处理、灵活的订阅机制、易于扩展

### 4. 工厂模式

**应用场景**：Runtime 初始化

```cpp
// RuntimeFactory::create() 内部五阶段初始化
static void init_infrastructure(Runtime& runtime);   // HTTP 工作流 + WorkspaceManager
static void init_memory_system(Runtime& runtime);     // MemoryStore + ContextBuilder + HistoryDB
static void init_tool_system(Runtime& runtime);       // 工具注册 + 技能发现 + MCP
static void init_orchestration(Runtime& runtime);     // WorkflowEngine + SubAgentRuntime + 插件
static void inject_agent_defaults(Runtime& runtime);  // 注入默认服务实现
```

**优势**：集中创建逻辑、易于维护、支持自定义

### 5. 组合模式

**应用场景**：角色工具过滤

```cpp
;
```

## 性能优化

### 1. 连接池 + ObjectPool

```cpp
class ConnectionPool {
    Task<TcpStream> acquire(host, port);
    void release(host, port, stream);
    void cleanup_idle();
    Task<void> warmup(EventLoop& loop, bool tls, host, port, count);
    // 内部 shared_mutex 读写锁
};
```

ObjectPool 集成减少堆分配：
- `enable_object_pool`（默认 true）控制是否启用
- `PooledConnection` 通过 `object_pool_->create()`/`destroy()` 复用内存
- `object_pool_stats()` 暴露利用率指标

### 2. 异步 I/O

```cpp
net::Task<ChatResult> chat_async(net::EventLoop& loop, const ChatRequest& request);
net::Task<StreamResult> chat_stream_async(...);
```

### 3. 零拷贝

```cpp
void on_token(std::string_view token);     // 避免 string 复制
void on_thinking(std::string_view token);   // 避免 string 复制
```

### 4. 读空闲超时保护

```cpp
// EventLoop::close_after 在读空闲超时时关闭 fd 并唤醒挂起的 I/O 协程
// 超时按"两次数据到达之间的最大间隔"计算，而非整体时间
// 每次 read_some 成功后自动刷新超时，LLM 流式长响应不会被误杀
const auto timeout = pool->config().response_timeout; // 默认 60s
loop->close_after(fd, timeout);
// 每次读到数据后：刷新超时
refresh_timeout();  // cancel_close + close_after
```

异常类型 `ResponseTimeoutError` 继承 `std::runtime_error`，不会被 HTTP 重试逻辑重试。

### 5. 核心调度线程池

```cpp
// 通过 ServiceRegistry 获取核心调度线程池
auto* pool = services().resolve<base::concurrency::ThreadPool>();
if (pool) { /* 使用线程池 */ }
```
```

### 6. CJK 感知 token 估算

```cpp
static int64_t estimate_text_tokens(std::string_view text);
// CJK 字符 = 1 token，ASCII = 0.25 token
```

## 错误处理

### 1. 异常处理

```cpp
try {
    auto result = co_await provider.chat_with_tools_async(loop, history, tools);
} catch (const std::exception& e) {
    log::error_fmt("Chat failed: {}", e.what());
}
```

### 2. 错误传播

```cpp
ToolResult result = ToolResult::execution_error(name, error_message);
ToolResult result = ToolResult::not_found(name);
ToolResult result = ToolResult::unknown_error(name);
```

### 3. 重试机制

```cpp
// 异步重试（OpenAI/Anthropic 共用）
auto result = co_await with_retry_async(loop, settings, "operation", [&] {
    return provider_.chat_async(...);
});

// 异步 HTTP 重试（重试原始 HTTP 请求，成功后应用 transform）
auto result = co_await with_http_retry_async(loop, settings, "operation",
    [&] { return http_post_async(...); },
    [](auto&& resp) { return parse(resp); }
);
```

### 4. 备用模型故障转移

```cpp
// ProviderClient 内置故障转移
// 主模型失败 → 自动切换 fallback_models 中的下一个可用模型
// 冷却追踪：per-model 指数退避 + 30s 探针
auto result = co_await provider.chat_with_tools_async(loop, history, tools);
// 内部流程：
// 1. 尝试主模型
// 2. 失败 → CooldownTracker.record_failure() → 尝试 fallback[0]
// 3. fallback[0] 也失败 → 尝试 fallback[1]
// 4. 成功 → CooldownTracker.record_success() → 清除冷却
```

错误分类（`ProviderErrorKind`）：rate_limit / transient / timeout / auth_error / billing_error / model_not_found / context_overflow / bad_request

### 5. Token 用量与延迟追踪

```cpp
// ProviderClient 自动采集每次 LLM 请求的 token 用量和延迟
// 4 个公开方法（chat_async / chat_with_tools_async / chat_stream_async / chat_stream_with_tools_async）
// 均内置：计时 → 请求 → TTFB 捕获 → usage 提取 → UsageTracker 记录 → 日志
auto result = co_await provider.chat_stream_with_tools_async(loop, history, tools, {}, handlers);
// result.usage: prompt_tokens + completion_tokens + cached_tokens
// result.latency: total_seconds + ttfb_seconds
// provider.usage_tracker(): 累计统计（线程安全）

// 终端自动显示（CLI CliApp → agent::StreamEventSink::on_response_stats → Renderer::on_usage_stats）
// ──── ↑9891 ↓17  1.23s (ttfb 0.45s)
```

关键设计：
- `TtfbCapture` 独立文件，避免 `usage.hpp` ↔ `stream.hpp` 循环依赖
- `llm::ConversationHistory` 已从 `workspace` 迁移至 `llm` 模块，打破 `llm ↔ workspace` 循环依赖
- 裁剪逻辑抽离为 `memory::PruneUtils`（自由函数），`memory → llm → ???` 变为单向 DAG
- OpenAI 流式添加 `stream_options: {include_usage: true}`
- `UsageTracker::last_actual_prompt_tokens()` 用于压缩判断校准

### 6. 上下文溢出自动恢复

当 LLM API 返回 `400 context_length_exceeded` 时，自动执行渐进式恢复：

```
请求 → context_overflow → force_compact → 重试 → 成功/失败
```

**恢复策略**（5 级逐级加码，L0→L4）：

| 级别 | 裁剪策略 | 压缩策略 |
|------|---------|---------|
| L0 | 默认裁剪参数 | 强制压缩（默认 keep_recent） |
| L1 | hard_prune_after=5, max_chars=1000 | keep_recent 减半 |
| L2 | hard_prune_after=3, max_chars=600 | — |
| L3 | 全量裁剪(hard=0), max_chars=400 | keep_recent=3 |
| L4 | 全量裁剪, max_chars=200 | keep_recent=1 |

**关键设计**：
- **优先裁剪**：每级先调裁剪参数（纯本地零开销）→ 估算 token → 不够再压缩（调 LLM 生成摘要）
- **估算驱动**：`ContextBuilder::estimate_messages_tokens()` 估算压缩后 token，70% 安全线以下即可重试
- **LLM 调用限制**：最多 5 次压缩调用，防止无限循环
- **统一检测**：`detect_context_overflow(status, body)` 仅 status==400 时查 body，正常路径零开销
- `ChatResult::is_context_overflow` / `StreamResult::is_context_overflow` 由 ProviderClient 统一标记
- 流式 + 非流式路径均支持

### 7. MemoryUpdater 重试

```cpp
for (int attempt = 1; attempt <= max_retries_; ++attempt) {
    try {
        response = chat_fn(prompt);
        if (!response.empty()) break;
    } catch (const std::exception& e) {
        log::warn_fmt("MemoryUpdater failed, attempt={}/{}: {}", attempt, max_retries_, e.what());
    }
    std::this_thread::sleep_for(std::chrono::seconds(attempt));
}
```

## 安全设计

### 跨进程文件锁

MemoryStore 写入使用 `FileLock` 实现跨进程互斥：
1. 获取排他文件锁
2. 截断文件
3. 写入新内容
4. fsync 确保数据落盘
5. RAII 析构自动释放锁

### 安全子进程

MCP 服务器通过 `subprocess::spawn` 启动：
- POSIX: `fork()` + `execvp()`，直接传递 argv/envp
- Windows: `CreateProcess()`
- 不经过 shell，避免命令注入

### 目录遍历防护

WorkspaceManager 校验工作空间名称：
- 禁止 `/`、`\`、`..`、`.` 前缀、`:`、`\0`
- 长度限制 128 字符

## 测试策略

### 1. 单元测试

```cpp
TEST(MemoryStoreTest, ReadWrite) {
    MemoryStore store(tier_paths);
    store.write_memory("test content", Tier::user);
    EXPECT_FALSE(store.read_memory().empty());
}
```

### 2. 集成测试

```cpp
TEST_F(WorkspaceTest, CreateAndRestore) {
    auto meta = mgr->create("test-ws");
    ASSERT_TRUE(meta.has_value());
    EXPECT_TRUE(mgr->remove("test-ws"));
    EXPECT_TRUE(mgr->restore("test-ws"));
}
```

### 3. 性能测试

```bash
./build/performance_benchmark
```

## 扩展点

### 1. 新增 LLM 提供商

不再修改 `provider_client.cpp` 的 if/else 分发，而是通过静态 registrar 注册：

```cpp
// 1. 实现 ProviderFactory（返回 ClientFns）
auto make_fns = ProviderClient::ClientFns;
auto make_my_provider_fns = [](const config::Settings& settings,
                                std::shared_ptr<net::HttpClient> http) {
    ProviderClient::ClientFns fns;
    auto client = std::make_shared<MyProviderClient>(settings, http);
    fns.chat_async = [client](auto& loop, auto& req, auto& cancel) {
        return client->chat_async(loop, req, cancel);
    };
    // ... 其余四个函数同理
    return fns;
};

// 2. 静态注册（编译期自动注册）
BEN_GEAR_REGISTER_PROVIDER(my_provider, make_my_provider_fns);

// 3. 在 config 枚举 Provider 中添加 my_provider 值
```

### 2. 新增插件

```cpp
// 插件代码（编译为 my_plugin.dll / my_plugin.so）
extern "C" void ben_gear_plugin_init() {
    BEN_GEAR_REGISTER_CAPABILITY("from_plugin", PluginCapability);
    // 或注册 Provider
    BEN_GEAR_REGISTER_PROVIDER(my_provider, make_my_provider_fns);
}
```

配置 `plugins_dir`，启动时自动加载。

### 3. 新增工具

### 4. 自定义事件消费

```cpp
// 方式一（推荐）：订阅 EventBus
auto sub = event_bus.subscribe<agent::TokenEvent>([](const agent::TokenEvent& e) {
    renderer.on_token(e.token);
});

// 方式二（向后兼容）：实现 ISP 接口，通过 AgentEventSinks 聚合
class MyStreamSink : public agent::StreamEventSink {
    void on_token(std::string_view token) const override { /* 自定义处理 */ }
    void on_thinking(std::string_view token) const override { /* 自定义处理 */ }
    void on_response_stats(...) const override {}
};
// 注意：SessionRunConfig 不再接受 AgentEventSinks
// 需手动通过 EventBus 或 Application 层传递
```


## 未来规划

### 短期
- [x] 流式工具调用（增量解析）
- [x] 工具调用超时控制
- [x] MCP HTTP 传输支持
- [x] 记忆系统（MemoryStore、EpisodeStore、Compactor、MemoryUpdater）
- [x] 工作空间管理（WorkspaceManager、Session）
- [x] Runtime 共享资源模式（替代 Runtime）
- [x] 安全子进程（fork+execvp）
- [x] 跨进程文件锁
- [x] IoContext 统一 I/O 管理（3 层分离：io/workflow/util）
- [x] 交互式 REPL（行编辑、历史记录、/ 命令补全）
- [x] 终端渲染子系统模块化（render/ + repl/ 分离）
- [x] ACP 统一协议层（消息/内容块/编解码/流式/适配器）— 位于 `src/acp/`（独立一级模块）
- [x] 工作流引擎（DAG 调度、命名空间隔离、模板库、人工审批）
- [x] Emoji 表情对齐修复（Rich 兼容的 display_width）
- [x] H3+ 子内容缩进
- [x] --md-raw CLI 选项
- [x] 头文件与源文件分离（hpp 声明 + cpp 实现，编译加速 + 依赖隔离）
- [x] TLS 抽象层（TlsEngine 接口，MbedTLS/OpenSSL/Schannel 多后端）
- [x] 压缩抽象层（CompressEngine 接口，zlib 后端）
- [x] 自研轻量测试框架（零 gtest/gmock/glog 依赖）
- [x] 零外部依赖（移除 nlohmann/json、googletest、glog 第三方源码）

### 中期
- [x] 备用模型故障转移 + 冷却退避
- [x] 上下文裁剪（ContextPruner 三级策略）
- [x] 增量裁剪优化（冻结区跳过 + token 缓存，长对话 ~9× 加速）
- [x] 上下文溢出自动恢复（L0→L4 渐进式裁剪+压缩）
- [x] 多 Agent 协作（设计已完成，见 [三种运行模式设计](design_three_modes.md)）
- [ ] 技能市场
- [x] Web UI

### 长期
- [x] 插件系统
- [ ] 分布式部署
- [ ] 模型微调集成

## Server 模块架构

### 分层设计

```
┌─────────────────────────────────────────────────────────┐
│  UI 层（Web 前端 / 远程 CLI / 第三方 HTTP 客户端）       │
├─────────────────────────────────────────────────────────┤
│  接入层                                                  │
│  ├─ WebSocket Handler — WsMessage v1 协议               │
│  ├─ HTTP Router — Trie 基路由（O(k) 匹配，替代 O(n) 遍历）│
│  └─ Static Files — 前端资源                              │
├─────────────────────────────────────────────────────────┤
│  API 层（依赖注入，通过按能力拆分的 `*_types.hpp` 抽象类解耦）│
│  ├─ SessionService — 会话 CRUD（虚拟基类）              │
│  ├─ ConfigService — 配置/模型/工作空间                   │
│  ├─ McpService — MCP 状态                               │
│  ├─ FileService — 文件浏览                              │
│  └─ RuntimeService — Agent 运行时控制                   │
├─────────────────────────────────────────────────────────┤
│  服务层                                                  │
│  ├─ WsSessionManager — WS 会话生命周期（从 Server 提取） │
│  ├─ SessionPool — LRU 会话池 + 并发锁                   │
│  └─ AuthService — Bearer Token 认证                     │
├─────────────────────────────────────────────────────────┤
│  核心层（复用，不重复实现）                               │
│  ├─ Runtime + Session                                   │
│  ├─ ProviderClient（LLM 客户端）                        │
│  └─ ToolRegistry + ToolCallManager                      │
└─────────────────────────────────────────────────────────┘
```

### 回调桥接

`WsSessionManager` 管理 WS 会话的创建、聊天、计划确认等生命周期操作。

### 依赖注入

API 层通过按能力拆分的 `*_types.hpp` virtual base class 与底层解耦，服务注册使用 `IApiServiceRegistry` 接口（位于 `server/composition/api_service_registry.hpp`）：

- `session_types.hpp` / `SessionService` — 会话操作（list/create/delete/rename/load_history）
- `config_types.hpp` / `ConfigService` — 配置读写（get_config/set_model）
- `workspace_types.hpp` / `WorkspaceService` — 工作空间列表
- `mcp_types.hpp` / `McpService` — MCP 状态
- `file_types.hpp` / `FileService` — 文件浏览（home/list）

### Web 前端

- **技术栈**：Vite 6 + Vue 3 + TypeScript
- **布局**：顶栏 + 左侧工作空间/会话导航 + 聊天区 + 可折叠右侧 TODO 面板
- **主题**：多套深色/亮色主题，CSS 变量驱动，保留原主题并新增参考项目亮色主题
- **通信**：WebSocket 双向，保持 `WsMessage v1`，计划/TODO/执行事件使用结构化消息类型
- **渲染**：marked + highlight.js Markdown 渲染，工具调用和执行事件独立折叠展示
- **状态**：计划草稿、TODO、消息流按 `workspace + session_id` 隔离并持久化

详细设计见 [Server 模式](server_mode.md) 和 [Web 计划模式与执行 TODO](web_plan_todo.md)。
