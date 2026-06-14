# BenGear 三种运行模式设计方案

> 版本：v2.0 | 日期：2026-06-13
> 状态：执行中（Server 后端已完成，Web 前端基础 UI 已落地，OpenAI API 开发中）

---

## 1. 概述

BenGear 作为 C++20 AI Agent 框架，需要支持三种运行模式，覆盖从个人开发到团队协作的全场景：

| 模式 | 定位 | 典型场景 |
|------|------|---------|
| **Single Agent 交互** | 单 Agent CLI 对话 | 个人开发助手、快速问答 |
| **Multi-Agent 协作** | Lead Agent 派遣 SubAgent | 复杂任务分解、并行调研、代码审查流水线 |
| **Server 服务** | HTTP SSE + JSON API | Web UI、IDE 插件、CI/CD 集成、多人共享 |

三种模式共享同一套核心架构（SharedResources + Session + Agent），差异仅在顶层调度和 I/O 适配层。

---

## 2. 现有架构基础

### 2.1 核心组件关系

```
┌──────────────────────────────────────────────────────┐
│  Agent（无状态调度器）                                │
│  - run_session_async(loop, session, prompt, cb)      │
│  - 不持有 ConversationHistory                        │
├──────────────────────────────────────────────────────┤
│  SharedResources（按 user+ws 构建一次）               │
│  ├─ ProviderClient（LLM 客户端）                     │
│  ├─ ToolRegistry（工具注册表，shared_mutex）          │
│  ├─ ToolCallManager（工具调度，共享 core_pool_）      │
│  ├─ MemoryStore（三层记忆）                          │
│  ├─ ContextBuilder（上下文构建）                     │
│  ├─ HistoryDB（会话持久化）                          │
│  ├─ SkillLoader（技能加载）                          │
│  ├─ MCPManager（MCP 服务）                          │
│  ├─ WorkflowEngine（工作流引擎，std::async）         │
│  ├─ WorkflowTemplateLibrary（全局模板库）            │
│  └─ core_pool_（核心调度线程池 2-8 线程）            │
├──────────────────────────────────────────────────────┤
│  Session（会话隔离单元）                              │
│  ├─ ConversationHistory（独占）                     │
│  ├─ EventLoop（独占）                               │
│  ├─ Compactor + MemoryUpdater（独占）               │
│  └─ EpisodeStore（会话级，独占）                     │
└──────────────────────────────────────────────────────┘
```

### 2.2 已有能力

| 能力 | 状态 | 说明 |
|------|------|------|
| SharedResources 共享 | ✅ 已实现 | 按 (user, ws) 构建，多 Agent/Session 复用 |
| Session 隔离 | ✅ 已实现 | 独占 history/event_loop/compactor |
| Agent 无状态 | ✅ 已实现 | `run_session_async` 接受 Session 引用 |
| WorkflowEngine | ✅ 已实现 | DAG 调度、命名空间隔离、模板库 |
| 核心线程池 | ✅ 已实现 | core_pool_ 共享，I/O 用 std::async |
| 三层记忆 | ✅ 已实现 | global → user → workspace |
| 命名空间隔离 | ✅ 已实现 | `user::ws::session::workflow_id` |

### 2.3 现有 CLI 入口（Single Agent 模式）

```
main.cpp
  ├─ 解析 CLI 参数 → Config
  ├─ 构建 WorkspaceContext (username, workspace_name, session_id)
  ├─ Agent(config, ws_ctx)  →  内部创建 SharedResources
  ├─ Session(SessionConfig, deps, tools)
  └─ loop.run(agent.run_session_async(...))
```

---

## 3. 模式一：Single Agent 交互

### 3.1 现状

已完整实现，即当前 `./bengear` 的 CLI 交互模式。

### 3.2 架构

```
┌───────────────┐
│  CLI (main)   │  stdin/stdout
└───────┬───────┘
        │ prompt
        ▼
┌───────────────┐       ┌──────────────────┐
│  Agent        │──────▶│ SharedResources  │
│  (1 instance) │       │ (1 instance)     │
└───────┬───────┘       └──────────────────┘
        │
        ▼
┌───────────────┐
│  Session      │
│  (1 instance) │
└───────────────┘
```

### 3.3 交互流程

```cpp
// main.cpp 伪代码
Agent agent(config, ws_ctx);                         // 创建 Agent + SharedResources
auto deps = agent.resources()->make_session_deps();
Session session({session_id, context_len}, deps, tools); // 创建 Session

while (true) {
    auto prompt = read_line("> ");
    if (prompt == "/exit") break;

    CancellationToken cancel;
    install_sigint_handler(cancel);

    auto result = loop.run(
        agent.run_session_async(loop, session, prompt, callbacks, cancel)
    );

    // 流式回调 on_token/on_tool_call 实时输出
}
```

### 3.4 优化方向

| 项目 | 现状 | 改进 |
|------|------|------|
| 会话恢复 | 每次启动新会话 | `--session-id` 恢复历史 |
| 多轮压缩 | Compactor 已实现 | 需验证长对话压缩质量 |
| 退出保存 | 自动持久化到 HistoryDB | 已满足 |

---

## 4. 模式二：Multi-Agent 协作

### 4.1 设计目标

- Lead Agent 通过 `delegate_task` / `delegate_tasks` 工具委派 SubAgent
- SubAgent 拥有独立 Session、独立上下文窗口和 `SessionType::sub_agent` 标识
- 支持多 SubAgent 并行执行，受 `SubAgentConfig::max_parallel` 控制
- SubAgent 执行过程通过 `SubAgentEvent` 回调给父 Agent/UI
- 子 Agent 输出可截断或经 LLM 汇总后回传给 Lead Agent

### 4.2 核心架构

```
┌────────────────────────────────────────────────────────────┐
│  SharedResources（1 instance，按 user+workspace 共享）      │
│  ├─ core_pool_（核心调度线程池）                            │
│  ├─ ToolRegistry（注册 delegate_task / delegate_tasks）     │
│  └─ SubAgentRuntime（统一管理子 Agent 生命周期）             │
├────────────────────────────────────────────────────────────┤
│  Lead Agent                                                 │
│  ├─ Session (main)                                          │
│  └─ Tool: delegate_task / delegate_tasks                    │
│                         │                                  │
│                         ▼                                  │
│  ┌─────────────────────┐  ┌─────────────────────┐          │
│  │  SubAgent A         │  │  SubAgent B         │  并行    │
│  │  ├─ Session A       │  │  ├─ Session B       │          │
│  │  └─ Event stream    │  │  └─ Event stream    │          │
│  └─────────────────────┘  └─────────────────────┘          │
│          │                         │                        │
│          └──────────┬──────────────┘                        │
│                     ▼                                       │
│       SubAgentResult 回传 Lead Agent（tool return value）    │
└────────────────────────────────────────────────────────────┘
```

### 4.3 当前类型定义

```cpp
struct SubAgentConfig {
    int max_parallel = 5;
    int default_max_steps = 20;
    std::chrono::milliseconds default_timeout{120000};
    bool auto_summary = true;
    int max_output_chars = 4000;
    container::Vector<container::String> tool_filter_default;
    container::String model_override;
    int64_t context_length_override = 0;
    bool aggregate_parallel = true;
};

struct SubAgentTask {
    container::String id;
    container::String prompt;
    container::String system_prompt;
    container::Vector<container::String> tool_filter;
    int max_steps = 0;
    std::chrono::milliseconds timeout{0};
    container::Vector<container::String> speculative_models;
};

struct SubAgentResult {
    container::String task_id;
    bool success = false;
    SubAgentStatus status = SubAgentStatus::pending;
    container::String output;
    container::String full_output;
    container::String error;
    llm::TokenUsage usage;
    llm::RequestLatency latency;
    int tool_steps = 0;
    Json artifacts;
    bool was_truncated = false;
    bool was_summarized = false;
};
```

### 4.4 工具注册

`include/ben_gear/tools/sub_agent_tools.hpp` 暴露注册入口：

```cpp
void register_sub_agent_tools(
    llm::ToolRegistry& registry,
    std::shared_ptr<agent::SubAgentRuntime> runtime);
```

注册后可用工具：

| 工具 | 用途 |
|------|------|
| `delegate_task` | 委派单个任务给一个 SubAgent |
| `delegate_tasks` | 并行委派多个任务，并按配置聚合结果 |

### 4.5 并行 SubAgent 调度

`delegate_tasks` 直接通过 `SubAgentRuntime` 执行多任务，受 `SubAgentConfig::max_parallel`、`default_timeout` 和 `default_max_steps` 约束。每个任务生成独立 `SubAgentTask`，完成后返回 `SubAgentResult` 列表；当 `aggregate_parallel=true` 时，可对并行结果做汇总。

### 4.6 嵌套深度控制

当前实现通过 `SubAgentRuntime` 管理父子关系和父会话 ID，并在工具层限制递归委派。后续若需要更强约束，可把最大深度提升为显式配置项，并在创建子 Agent 前统一校验。

### 4.7 资源隔离矩阵

| 资源 | Lead Agent | SubAgent | 说明 |
|------|-----------|----------|------|
| SharedResources | 共享 | 共享 | 同一实例，节省内存 |
| ToolRegistry | 共享 | 共享 | 继承 Lead 的全部工具 |
| Session | 独占 | 独占 | **各自独立**，互不干扰 |
| ConversationHistory | 独占 | 独占 | 上下文隔离 |
| EventLoop | 独占 | 独占 | 各自阻塞等待 |
| MemoryStore | 共享 | 共享 | 三层记忆全局可见 |
| EpisodeStore | 独占 | 独占 | 会话级情景记忆 |
| core_pool_ | 共享 | 共享 | 工具调用共用线程池 |

### 4.8 生命周期管理

```
Lead Agent run_session_async()
  │
  ├─ LLM 返回 tool_call: delegate_task/delegate_tasks
  │
  ├─ ToolCallManager.execute_tool()
  │   │
  │   ├─ SubAgentRuntime 创建 SubAgentTask
  │   ├─ 创建子 Agent 独立 Session（SessionType::sub_agent）
  │   ├─ 执行子 Agent 并向父回调发送 SubAgentEvent
  │   ├─ 截断/汇总输出
  │   └─ 返回 SubAgentResult
  │
  └─ Lead Agent 继续（结果写入 history）
```

**关键约束**：
- SubAgent 必须在 ToolCallManager 的线程池中执行（已有 `context_` 延长 SharedResources 生命周期）
- SubAgent 的 Session 目录在 `workspace_dir/sessions/<sub_session_id>/memory/`
- SubAgent 的历史也写入 HistoryDB，可后续审计

### 4.9 实现步骤

| 步骤 | 内容 | 改动文件 |
|------|------|---------|
| 1 | 定义 `SubAgentConfig` / `SubAgentResult` | 新建 `agent/sub_agent.hpp` |
| 2 | 实现 `delegate_task` / `delegate_tasks` 工具 | 新建 `tools/sub_agent_tools.hpp` |
| 3 | 在 `SharedResources::init_tools()` 注册 | 修改 `shared_resources.hpp` |
| 4 | 嵌套深度控制 | 修改 `shared_resources.hpp` |
| 5 | 添加配置项 `max_agent_depth` | 修改 `config/settings.hpp` |
| 6 | 集成测试 | 新建 `tests/test_multi_agent.cpp` |
| 7 | 更新文档 | 修改 `docs/` |

---

## 5. 模式三：Server 服务

### 5.1 设计目标

- HTTP API，支持 SSE 流式响应
- 多用户/多工作空间路由
- Agent 池 + Session 池
- 会话保持（同一用户可恢复历史会话）
- 健康检查 + 指标监控

### 5.2 API 设计

#### 5.2.1 会话管理

```
POST /v1/sessions
  Request:  { "user": "alice", "workspace": "project-x", "session_id": "" }
  Response: { "session_id": "a1b2c3d4e5f67890", "status": "created" }

GET /v1/sessions?user=alice&workspace=project-x
  Response: { "sessions": [...] }

DELETE /v1/sessions/{session_id}
  Response: { "status": "deleted" }
```

#### 5.2.2 对话

当前交互式对话走 WebSocket `chat` 消息，服务端通过 `ServerCallbacks` 持续推送 `token`、`thinking`、`tool_call`、`tool_result`、`sub_agent`、`done` 等消息。

OpenAI 兼容 HTTP 端点仍是计划项：

```
POST /v1/chat/completions  # 待实现：SSE/非流式 + 工具调用
GET  /v1/models            # 待实现：模型列表
```

#### 5.2.3 WebSocket 事件

```
client → server: { "type": "chat", "session_id": "...", "prompt": "你好" }
server → client: { "type": "token", "session_id": "...", "content": "你" }
server → client: { "type": "tool_call", "name": "read_file", "data": {...} }
server → client: { "type": "done", "data": { "usage": {...} } }
```

#### 5.2.4 当前 REST/管理接口

当前已注册会话、配置/工作空间、MCP、文件浏览 API。健康检查、指标、技能、工作流模板接口仍是后续计划。

### 5.3 当前核心架构

```
┌─────────────────────────────────────────────────────────────────┐
│  Server（基于 ben_gear::net）                                   │
│  ├─ Router：HTTP 路由、路径参数、CORS                           │
│  ├─ WsHandler：WebSocket 握手、帧处理、连接管理                  │
│  ├─ StaticFileServer：前端静态资源                              │
│  └─ AuthService：Bearer Token 或无认证模式                      │
├─────────────────────────────────────────────────────────────────┤
│  API 层                                                         │
│  ├─ SessionApi：会话 CRUD + 历史加载                            │
│  ├─ ConfigApi：模型/Provider/工作空间信息                       │
│  ├─ McpApi：MCP 状态                                            │
│  └─ FileApi：文件浏览                                           │
├─────────────────────────────────────────────────────────────────┤
│  SessionPool                                                    │
│  ├─ 按 (session_id, username, workspace) 查找或创建 Agent 条目   │
│  ├─ 条目内持有 Agent、SharedResources、WorkspaceContext         │
│  ├─ LRU 淘汰，容量由 agent_pool_max_size 控制                   │
│  └─ 条目 mutex 串行化同会话请求                                 │
└─────────────────────────────────────────────────────────────────┘
```

### 5.4 关键类型

| 类型 | 文件 | 说明 |
|------|------|------|
| `Server` | `include/ben_gear/server/core/server.hpp` | Server 主编排，负责路由注册、监听和连接处理 |
| `Router` | `include/ben_gear/server/core/router.hpp` | HTTP 路由匹配、路径参数和 CORS |
| `WsHandler` | `include/ben_gear/server/ws/handler.hpp` | WebSocket 连接、消息收发和广播 |
| `WsMessage` | `include/ben_gear/server/ws/protocol.hpp` | WebSocket v1 消息协议 |
| `SessionPool` | `include/ben_gear/server/session/pool.hpp` | 活跃会话 LRU 池和并发锁 |
| `ServerCallbacks` | `include/ben_gear/server/callback/server_callbacks.hpp` | Agent 事件到 WebSocket 消息的桥接 |
| `AuthService` | `include/ben_gear/server/auth/auth.hpp` | Bearer Token 认证 |

### 5.5 WebSocket 流式适配器

Server 当前以 WebSocket 作为交互式流式通道。`ServerCallbacks` 将 Agent 运行事件转换成 `WsMessage`，再由 `WsHandler` 推送给浏览器或远程客户端：

| Agent 事件 | WebSocket 消息 |
|------------|----------------|
| token 增量 | `token` |
| thinking 增量 | `thinking` |
| tool call | `tool_call` |
| tool result | `tool_result` |
| sub-agent event | `sub_agent` |
| response stats | `done` |
| error | `error` |

OpenAI 兼容 SSE 通道仍在设计中，计划由后续 `chat_api` 路由和 SSE writer 实现。

### 5.6 请求处理流程

```
HTTP/WebSocket Request → 认证 → 路由/升级 → Handler
                                      │
                                      ▼
                              ┌───────────────┐
                              │  SessionPool  │──→ get_or_create(session_id, username, workspace)
                              └───────┬───────┘
                                      │ AgentEntry
                                      ▼
                              ┌───────────────┐
                              │  Agent        │──→ run_session_async(loop, session, ...)
                              └───────┬───────┘
                                      │
                              ┌───────┴────────────┐
                              │  ServerCallbacks   │──→ WsHandler → Client
                              └────────────────────┘
```

### 5.7 并发模型

- Server 使用一个 `IoContext` 处理监听和连接协程。
- 每个连接解析一次 HTTP 请求；WebSocket 升级后由 `WsHandler` 管理连接状态。
- `SessionPool` 按 `(session_id, username, workspace)` 缓存 Agent 条目。
- 同一 Agent 条目内通过 mutex 串行化会话访问，避免同一会话并发写历史。
- 工具调用继续复用 `SharedResources` 内的核心线程池。

### 5.8 会话并发保护

当前没有单独的 `SessionLockManager` 类型；并发保护内聚在 `SessionPool` 的条目锁中。同一会话进入 Agent 执行前获取条目 mutex，不同会话可并行处理。

### 5.9 配置扩展

```json
// config.json 新增 server 字段
{
    "server": {
        "host": "0.0.0.0",
        "port": 8080,
        "max_concurrent_requests": 100,
        "session_idle_timeout_seconds": 1800,
        "agent_pool_max_size": 50,
        "cors_origins": ["*"],
        "api_key": ""   // 可选的 API 认证密钥
    }
}
```

```cpp
// config/settings.hpp 新增
struct ServerSettings {
    container::String host = container::String("0.0.0.0");
    int port = 8080;
    int max_concurrent_requests = 100;
    int session_idle_timeout_seconds = 1800;
    int agent_pool_max_size = 50;
    container::Vector<container::String> cors_origins;
    container::String api_key;  // 空=不认证
};

// Settings 中新增
ServerSettings server;
```

### 5.10 启动入口

当前实现通过 `serve` 子命令进入 Server 模式：

```cpp
.command("serve", "Start HTTP/WebSocket server", [&](const cli::Parsed&) {
    ensure_loaded();
    ben_gear::server::Server srv(config);
    std::cout << "BenGear server listening on "
              << std::string(config.server.host.c_str())
              << ":" << config.server.port << std::endl;
    srv.run();
    std::exit(0);
})
```

`Server::setup_routes()` 注册会话、配置/工作空间、MCP、文件 API，并由 `handle_connection()` 分流 WebSocket、REST 和静态文件。OpenAI 兼容 `/v1/chat/completions` 和 SSE writer 仍处于待实现阶段。

### 5.11 实现状态

| 步骤 | 当前状态 | 说明 |
|------|----------|------|
| `ServerSettings` | ✅ 已定义 | `include/ben_gear/config/settings.hpp` 已包含默认值 |
| `SessionPool` | ✅ 已实现 | LRU 容量由 `agent_pool_max_size` 控制 |
| HTTP Router | ✅ 已实现 | 支持路径参数与 CORS |
| WebSocket | ✅ 已实现 | `WsHandler` + `WsMessage` v1 协议 |
| `ServerCallbacks` | ✅ 已实现 | Agent 事件桥接到 WS 消息 |
| REST API | ✅ 部分完成 | 会话/配置/工作空间/MCP/文件已注册 |
| OpenAI Chat API | 🚧 待实现 | `ChatService` 预留，`chat_api` 路由未注册 |
| JSON `server` 配置解析 | 🚧 待实现 | 结构存在，loader 尚未读取配置文件字段 |
| 集成测试 | 🚧 待补充 | 需覆盖 WS/REST/认证/会话隔离 |

---

## 6. 三种模式的共享与差异

### 6.1 组件共享矩阵

| 组件 | Single Agent | Multi-Agent | Server |
|------|:-----------:|:-----------:|:------:|
| SharedResources | 1 实例 | 1 实例（共享） | SessionPool 条目内持有 |
| Agent | 1 实例 | N 实例（Lead + Sub） | N 实例（按会话缓存） |
| Session | 1 实例 | N 实例（各自独占） | SessionPool 管理 |
| ToolRegistry | 1 实例 | 1 实例（共享） | N 实例（随 SharedResources） |
| core_pool_ | 1 实例 | 1 实例（共享） | 1 全局实例 |
| WorkflowEngine | 1 实例 | 1 实例（共享） | 1 全局实例 |
| EventLoop | 1 实例 | N 实例（随 Session） | N 实例（随请求） |

### 6.2 I/O 适配层差异

| 模式　　　　 | 输入　　　| 输出　　　　　　| 回调实现　　　　　　　　　　　　 |
| --------------| -----------| -----------------| ----------------------------------|
| Single Agent | stdin　　 | stdout/stderr　 | `TerminalAgentCallbacks`　　　　 |
| Multi-Agent　| tool args | tool return　　 | `NullAgentCallbacks`（SubAgent） |
| Server　　　 | HTTP / WebSocket JSON | WebSocket / HTTP JSON | `ServerCallbacks`　　　　　　 |

### 6.3 启动方式

```bash
# 模式一：交互式（默认）
./bengear
./bengear --user alice --workspace project-x

# 模式二：Multi-Agent（自动，由 LLM 决定是否委派子 Agent）
./bengear  # 与模式一相同，LLM 可调用 delegate_task/delegate_tasks 工具

# 模式三：Server
./bengear serve
```

---

## 7. 配置统一

### 7.1 配置层级

```
全局默认值（代码内置）
    ↓ 覆盖
~/.bengear/config.json（全局配置）
    ↓ 覆盖
项目目录/.bengear.json（项目配置）
    ↓ 覆盖
环境变量 / CLI 参数（运行时配置）
```

### 7.2 模式选择配置

```json
{
    "mode": "interactive",   // interactive | server
    "agent": {
        "max_tool_steps": 200,
        "max_tool_calls": 200,
        "max_tool_calls_per_step": 50,
        "max_agent_depth": 3,  // Multi-Agent 嵌套深度
        "command_timeout": 30
    },
    "server": {               // 仅 server 模式生效
        "host": "0.0.0.0",
        "port": 8080,
        "max_concurrent_requests": 100,
        "session_idle_timeout_seconds": 1800
    }
}
```

---

## 8. 错误处理与容错

### 8.1 Multi-Agent 容错

| 场景 | 处理策略 |
|------|---------|
| SubAgent 超时 | `max_tool_steps` 限制 + `command_timeout` 双重保护 |
| SubAgent 异常 | 返回 `SubAgentResult{success=false, error_message}` |
| SubAgent 嵌套过深 | `max_agent_depth` 硬限制，超出直接拒绝 |
| SharedResources 析构 | ToolCallManager 的 `context_` 延长生命周期 |

### 8.2 Server 容错

| 场景 | 处理策略 |
|------|---------|
| 请求超时 | WebSocket 发送 `error` 消息或 HTTP 返回错误响应 |
| Session 并发 | `SessionPool` 条目锁串行化同会话请求 |
| SessionPool OOM | `agent_pool_max_size` 限制 + LRU 淘汰 |
| 进程崩溃 | HistoryDB 持久化，重启后可恢复 |

---

## 9. 监控与可观测性

### 9.1 指标定义

| 指标 | 类型 | 说明 |
|------|------|------|
| `active_sessions` | Gauge | 当前活跃会话数 |
| `active_agents` | Gauge | 当前活跃 Agent 数 |
| `total_requests` | Counter | 总请求数 |
| `request_latency_ms` | Histogram | 请求延迟分布 |
| `tool_calls_total` | Counter | 工具调用总数 |
| `tool_call_latency_ms` | Histogram | 工具调用延迟 |
| `token_usage` | Counter | Token 消耗量 |
| `pool_size_bytes` | Gauge | 内存池使用量 |

### 9.2 日志追踪

所有模式共享统一的日志格式：

```
06-07 10:56:38 [info] [33131598] [user-ws-session] message
06-07 10:56:52 [debug] [33131599] [global] server listening on 0.0.0.0:8080
```

Multi-Agent 场景下，SubAgent 的 trace ID 自动添加 `:sub` 后缀：

```
06-07 10:57:00 [info] [33131600] [alice-project-a1b2c3:sub] sub-agent spawned
06-07 10:57:05 [info] [33131600] [alice-project-a1b2c3:sub] sub-agent completed
```

---

## 10. 测试策略

### 10.1 单元测试

| 测试 | 覆盖 |
|------|------|
| `SubAgentConfig` 序列化 | Multi-Agent 配置解析 |
| `SessionPool.get_or_create` | Session 创建/恢复/LRU 淘汰 |
| `SessionPool` entry mutex | 同会话并发保护 |
| `WsMessage` encode/decode | WebSocket 协议兼容性 |
| `Router.match` | 路径参数、CORS、404 行为 |

### 10.2 集成测试

| 测试 | 覆盖 |
|------|------|
| Lead Agent → delegate_task → SubAgent | 完整调用链 |
| 并行 SubAgent → 结果汇总 | delegate_tasks 并行执行与聚合 |
| Server → WebSocket chat | WS 消息收发与 Agent 回调桥接 |
| Server → REST API | 会话/配置/工作空间/MCP/文件接口 |
| Server → Session 恢复 | 断线重连 |
| Server → 多用户隔离 | 用户 A 看不到用户 B 的数据 |

### 10.3 压力测试

| 测试 | 指标 |
|------|------|
| 100 并发 Session | 延迟 P50/P99 |
| 10 并发 SubAgent | 资源占用 |
| Server 1000 RPS | 吞吐量 |

---

## 11. 实施路线图

### Phase 1：Multi-Agent 协作（P1）✅ 已完成

- [x] `SubAgentConfig` / `SubAgentResult` 类型定义
- [x] `SubAgent` 运行时
- [x] `delegate_task` / `delegate_tasks` 工具实现
- [x] 嵌套深度控制
- [x] 独立会话、并行执行、推测执行、LLM 聚合摘要
- [x] 集成测试 + 端到端验证

**结果**：已落地为子 Agent 系统，无破坏性变更

### Phase 2：Server 基础版（P2）✅ 已完成

- [x] `SessionPool`（LRU 淘汰 + 并发锁）
- [x] HTTP 路由 + Router（路径参数匹配 + CORS）
- [x] WebSocket 双向通信（WsHandler + WsMessage v1 协议）
- [x] ServerCallbacks（Agent→WS 推送桥接）
- [x] 会话/配置/工作空间/MCP API
- [x] 认证（Bearer Token + 无认证模式）
- [x] 静态文件服务
- [x] `ServerSettings` 默认值支持
- [ ] JSON `server` 配置解析（loader 待补）

**预计改动**：~1500 行新增代码，无破坏性变更

### Phase 3：Server 增强 + Web 前端（P3）🚧 开发中

- [x] 认证中间件（Bearer Token）
- [ ] OpenAI 兼容 API（`/v1/chat/completions` SSE 流式 + 工具调用）
- [x] Web 前端基础 UI（Vite + Vue 3，顶栏+左侧导航+聊天区布局）
- [x] 多主题切换（Obsidian/Midnight/Coral/Light）
- [ ] Web 前后端联调与生产可用性打磨
- [ ] 远程 CLI（`bengear-remote` WebSocket 客户端）
- [ ] 限流中间件
- [ ] 指标监控 `/v1/metrics`
- [ ] 优雅关闭
- [ ] Docker 镜像

**预计改动**：~800 行新增代码

---

## 12. 风险评估

| 风险 | 概率 | 影响 | 缓解措施 |
|------|------|------|---------|
| SubAgent 资源泄漏 | 中 | 高 | `SubAgentConfig` 的并行数、步数和超时限制 |
| Session 并发冲突 | 低 | 高 | `SessionPool` 条目 mutex 串行化 |
| Server 内存膨胀 | 中 | 中 | `SessionPool` LRU + `agent_pool_max_size` |
| WebSocket 连接断开 | 高 | 低 | 客户端自动重连 + Session 恢复 |
| ThreadPool 死锁 | 低 | 高 | 核心调度与 I/O 分离（已有设计） |

---

## 附录 A：目录结构（当前实施状态）

```
include/ben_gear/
├── agent/
│   ├── agent.hpp              # 无状态调度器
│   ├── shared_resources.hpp   # 共享资源
│   ├── sub_agent.hpp          # ✅ SubAgent 配置/结果/运行时
│   └── callbacks.hpp          # ✅ 回调接口（含 on_sub_agent_event）
├── server/                    # ✅ Server 模式（已完成基础架构）
│   ├── core/
│   │   ├── server.hpp         # ✅ Server 主编排
│   │   └── router.hpp         # ✅ HTTP 路由（路径参数 + CORS）
│   ├── http/
│   │   ├── parser.hpp         # ✅ HTTP 请求解析
│   │   └── static_files.hpp   # ✅ 静态文件服务
│   ├── auth/
│   │   └── auth.hpp           # ✅ Bearer Token 认证
│   ├── ws/
│   │   ├── handler.hpp        # ✅ WebSocket 帧处理
│   │   └── protocol.hpp       # ✅ WsMessage v1 协议
│   ├── api/
│   │   ├── deps.hpp           # ✅ 服务接口（依赖注入）
│   │   ├── handlers.hpp       # ✅ API 聚合注册
│   │   ├── session_api.hpp    # ✅ 会话 CRUD
│   │   ├── config_api.hpp     # ✅ 配置/模型/工作空间
│   │   ├── mcp_api.hpp        # ✅ MCP 状态
│   │   ├── file_api.hpp       # ✅ 文件浏览
│   │   └── chat_api.hpp       # 🚧 OpenAI 兼容 API（待实现，当前未创建）
│   ├── session/
│   │   └── pool.hpp           # ✅ Session 池（LRU + 并发锁）
│   └── callback/
│       └── server_callbacks.hpp # ✅ Agent→WS 回调桥接
├── tools/
│   ├── sub_agent_tools.hpp    # ✅ delegate_task/delegate_tasks
│   ├── builtin_tools.hpp
│   ├── memory_tools.hpp
│   ├── skill_tools.hpp
│   ├── workflow_tools.hpp
│   └── workspace_tools.hpp
└── ...

web/                           # 🚧 Web 前端（基础 UI 已落地，联调中）
├── package.json
├── vite.config.ts
├── src/
│   ├── App.vue                # Shell 布局
│   ├── service/               # HTTP + WebSocket 封装
│   ├── protocol/              # WsMessage 类型与编解码
│   ├── theme/                 # 多主题
│   ├── composables/           # 会话/配置/连接/消息状态
│   └── components/            # 登录、顶栏、导航、聊天、自定义块
└── ...
```

## 附录 B：配置完整示例

```json
{
    "provider": "openai",
    "model": "gpt-4o-mini",
    "api_key": "sk-xxx",
    "stream": true,

    "agent": {
        "max_tool_steps": 200,
        "max_tool_calls": 200,
        "max_tool_calls_per_step": 50,
        "max_agent_depth": 3,
        "command_timeout": 30,
        "system_prompt": ""
    },

    "server": {
        "host": "0.0.0.0",
        "port": 8080,
        "max_concurrent_requests": 100,
        "session_idle_timeout_seconds": 1800,
        "agent_pool_max_size": 50,
        "cors_origins": ["*"],
        "api_key": ""
    },

    "connection_pool": {
        "max_connections_per_host": 10,
        "idle_timeout_seconds": 30,
        "connect_timeout_seconds": 10,
        "response_timeout_seconds": 60,
        "enable_keep_alive": true
    },

    "thread_pool": {
        "min_threads": 2,
        "max_threads": 8,
        "max_queue_size": 1024
    },

    "username": "default",
    "workspace_name": "default"
}
```
