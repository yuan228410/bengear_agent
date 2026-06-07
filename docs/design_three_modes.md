# BenGear 三种运行模式设计方案

> 版本：v1.0 | 日期：2026-06-07  
> 状态：设计评审中

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

- Lead Agent 通过 `spawn_agent` 工具派遣 SubAgent
- SubAgent 拥有独立 Session，独立上下文窗口
- 支持 SubAgent 并行执行（通过 WorkflowEngine DAG）
- SubAgent 结果通过 tool return value 回传给 Lead Agent
- SubAgent 可嵌套派遣（最大深度限制）

### 4.2 核心架构

```
┌────────────────────────────────────────────────────────────┐
│  SharedResources（1 instance，按 user+ws 共享）            │
│  ├─ core_pool_（核心调度线程池）                           │
│  ├─ WorkflowEngine（共享，命名空间隔离）                   │
│  ├─ ToolRegistry（共享，SubAgent 继承 Lead 的工具集）      │
│  └─ ...                                                    │
├────────────────────────────────────────────────────────────┤
│                                                            │
│  ┌──────────────────────────────────────────┐              │
│  │  Lead Agent                              │              │
│  │  ├─ Session (lead_session)               │              │
│  │  └─ Tool: spawn_agent                    │──┐           │
│  └──────────────────────────────────────────┘  │           │
│                            │ spawn_agent 调用   │           │
│                            ▼                    │           │
│  ┌─────────────────────┐  ┌─────────────────────┐         │
│  │  SubAgent A         │  │  SubAgent B         │  并行   │
│  │  ├─ Session A       │  │  ├─ Session B       │         │
│  │  └─ Tool: spawn...  │  │  └─ Tool: spawn...  │         │
│  └─────────────────────┘  └─────────────────────┘         │
│          │                         │                       │
│          └──────────┬──────────────┘                       │
│                     ▼                                      │
│          结果回传 Lead Agent（tool return value）           │
└────────────────────────────────────────────────────────────┘
```

### 4.3 新增类型定义

```cpp
// agent/sub_agent.hpp

/// SubAgent 配置
struct SubAgentConfig {
    container::String task_description;  // 任务描述（作为 system prompt 补充）
    container::String session_id;        // 空=自动生成
    int max_tool_steps = 20;             // 子 Agent 工具调用上限（防止失控）
    int max_depth = 2;                   // 最大嵌套深度（1=不可再派遣）
    bool share_history = false;          // 是否继承 Lead 的历史（默认不继承）
};

/// SubAgent 执行结果
struct SubAgentResult {
    container::String session_id;
    container::String final_response;    // 最终回复文本
    int tool_calls_executed = 0;         // 执行的工具调用次数
    bool success = true;
    container::String error_message;
};
```

### 4.4 spawn_agent 工具注册

```cpp
// tools/agent_tools.hpp

inline void register_agent_tools(llm::ToolRegistry& registry,
                                  std::shared_ptr<agent::SharedResources> resources) {
    registry.register_tool(
        container::String("spawn_agent"),
        container::String(
            "Spawn a sub-agent to handle a specific task independently. "
            "The sub-agent has its own context window and can use tools. "
            "Returns the sub-agent's final response. "
            "Use for: research tasks, code analysis, parallel investigations, "
            "or any task that benefits from a dedicated context window."
        ),
        {
            {container::String("task_description"), llm::ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String(
                    "Clear, specific task description for the sub-agent. "
                    "This becomes part of the sub-agent's system prompt."
                )
            }},
            {container::String("prompt"), llm::ToolParameterSchema{
                .type = container::String("string"),
                .description = container::String(
                    "The actual message/question to send to the sub-agent."
                )
            }},
        },
        // executor：创建 SubAgent + Session → 执行 → 返回结果
        [resources](const Json& args) -> std::string {
            auto task_desc = args.value("task_description", "");
            auto prompt = args.value("prompt", "");

            // 1. 创建 SubAgent（共享 SharedResources）
            Agent sub_agent(resources);

            // 2. 创建独立 Session
            auto deps = resources->make_session_deps();
            Session sub_session(
                SessionConfig{container::String(), resources->settings().context_length},
                deps,
                resources->tools_mut()
            );

            // 3. 执行
            net::EventLoop loop;
            NullAgentCallbacks callbacks;
            CancellationToken cancel;

            auto result = loop.run(
                sub_agent.run_session_async(
                    loop, sub_session,
                    container::String(prompt.c_str()),
                    callbacks, cancel
                )
            );

            // 4. 返回结果
            Json output;
            output["response"] = std::string(result.raw.data(), result.raw.size());
            output["tool_calls"] = result.tool_calls.size();
            output["success"] = result.status >= 200 && result.status < 300;

            return output.dump();
        }
    );
}
```

### 4.5 并行 SubAgent 调度

通过 WorkflowEngine DAG 实现并行派遣：

```cpp
// Lead Agent 在 tool call 中调用 create_workflow + execute_workflow
// 工作流定义示例：
WorkflowDefinition parallel_research = {
    .id = "parallel_research",
    .name = "Parallel Research",
    .tasks = {
        {
            .id = "research_a",
            .type = "tool",
            .prompt = "spawn_agent",  // 调用 spawn_agent 工具
            .config = Json{
                {"task_description", "Research topic A"},
                {"prompt", "Find information about..."}
            }
        },
        {
            .id = "research_b",
            .type = "tool",
            .prompt = "spawn_agent",  // 并行调用
            .config = Json{
                {"task_description", "Research topic B"},
                {"prompt", "Analyze the following..."}
            }
        },
        {
            .id = "synthesize",
            .type = "llm",
            .prompt = "Based on the following research:\n"
                      "A: {research_a}\n\n"
                      "B: {research_b}\n\n"
                      "Provide a comprehensive summary.",
            .depends_on = {"research_a", "research_b"}
        }
    }
};
```

### 4.6 嵌套深度控制

```cpp
// SharedResources 中记录当前 Agent 嵌套深度
// 通过 thread_local 或命名空间传递

class SharedResources {
    // 最大嵌套深度（配置项）
    int max_agent_depth_ = 3;

    // 在 spawn_agent executor 中检查
    bool can_spawn() const {
        auto& ns = workflow::WorkflowEngine::get_current_namespace();
        // 解析命名空间中的深度信息
        int depth = count_char(ns, ':') / 2;  // user::ws::session = depth 1
        return depth < max_agent_depth_;
    }
};
```

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
  ├─ LLM 返回 tool_call: spawn_agent
  │
  ├─ ToolCallManager.execute_tool()
  │   │
  │   ├─ 创建 SubAgent(resources)      ← 共享 SharedResources
  │   ├─ 创建 Session(deps, tools)     ← 独立 Session
  │   ├─ loop.run(sub_agent.run_session_async(...))
  │   ├─ 等待结果
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
| 2 | 实现 `spawn_agent` 工具 | 新建 `tools/agent_tools.hpp` |
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

```
POST /v1/chat/completions
  Headers:  Content-Type: application/json
            Accept: text/event-stream (流式) 或 application/json (非流式)
  Request:  {
              "session_id": "a1b2c3d4e5f67890",
              "messages": [{ "role": "user", "content": "你好" }],
              "stream": true,
              "model": "gpt-4o-mini",
              "temperature": 0.2,
              "max_tokens": 1024
            }
  Response (SSE):
    data: {"id":"chatcmpl-xxx","choices":[{"delta":{"content":"你"},"index":0}]}
    data: {"id":"chatcmpl-xxx","choices":[{"delta":{"content":"好"},"index":0}]}
    data: [DONE]

  Response (非流式):
    { "id": "chatcmpl-xxx", "choices": [{"message": {...}}], "usage": {...} }
```

#### 5.2.3 工具调用事件（SSE 扩展）

```
event: tool_call
data: {"id":"call_xxx","name":"read_file","arguments":{}}

event: tool_result
data: {"id":"call_xxx","success":true,"output_size":1024}

event: thinking
data: {"content":"正在分析..."}

event: done
data: {}
```

#### 5.2.4 管理接口

```
GET  /v1/health          → { "status": "ok", "uptime": 3600 }
GET  /v1/metrics         → { "active_sessions": 5, "total_requests": 1024, ... }
GET  /v1/skills          → { "skills": [...] }
GET  /v1/workflows        → { "templates": [...] }
```

### 5.3 核心架构

```
┌─────────────────────────────────────────────────────────────────┐
│  HTTP Server (基于 ben_gear::net)                               │
│  ├─ 路由层（URL → Handler 映射）                                │
│  ├─ 中间件层（认证、限流、CORS）                                │
│  └─ SSE 编码层（text/event-stream）                             │
├─────────────────────────────────────────────────────────────────┤
│  AgentPool                                                      │
│  ├─ 按 (user, workspace) 查找或创建 SharedResources             │
│  ├─ LRU 淘汰不活跃的 SharedResources                            │
│  └─ 线程安全（shared_mutex）                                    │
├─────────────────────────────────────────────────────────────────┤
│  SessionPool                                                    │
│  ├─ 按 session_id 查找或创建 Session                            │
│  ├─ 空闲超时自动归档（释放 EventLoop 等独占资源）               │
│  └─ 线程安全（shared_mutex）                                    │
├─────────────────────────────────────────────────────────────────┤
│  共享层                                                         │
│  ├─ core_pool_（全局核心线程池）                                │
│  ├─ WorkflowEngine（全局共享）                                  │
│  └─ WorkflowTemplateLibrary（全局只读）                         │
└─────────────────────────────────────────────────────────────────┘
```

### 5.4 关键类型

```cpp
// server/agent_pool.hpp

/// Agent 池 — 按 (user, workspace) 复用 SharedResources
class AgentPool {
public:
    /// 获取或创建 SharedResources
    /// 命中缓存时直接返回；未命中时创建并缓存
    std::shared_ptr<agent::SharedResources> get_or_create(
        const container::String& user,
        const container::String& workspace,
        const config::Settings& settings_template
    ) {
        std::string key = make_key(user, workspace);

        {
            std::shared_lock lock(mutex_);
            auto it = pool_.find(key);
            if (it != pool_.end()) {
                return it->second;
            }
        }

        std::unique_lock lock(mutex_);
        // double-check
        auto it = pool_.find(key);
        if (it != pool_.end()) {
            return it->second;
        }

        auto ws_ctx = build_ws_ctx(user, workspace);
        auto resources = std::make_shared<agent::SharedResources>(
            settings_template, std::move(ws_ctx));
        resources->post_init();
        pool_[key] = resources;

        log::info_fmt("agent pool: created resources for user={} ws={}",
                      user, workspace);
        return resources;
    }

    /// 淘汰不活跃的资源（LRU）
    void evict_idle(std::chrono::seconds max_idle) { /* ... */ }

    /// 获取池状态
    struct PoolStats {
        size_t total_entries;
        size_t active_sessions;
        size_t total_memory_mb;
    };
    PoolStats stats() const { /* ... */ }

private:
    static std::string make_key(const container::String& user,
                                const container::String& ws) {
        return std::string(user.c_str()) + "::" + std::string(ws.c_str());
    }

    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<agent::SharedResources>> pool_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> last_access_;
};
```

```cpp
// server/session_pool.hpp

/// Session 池 — 按 session_id 管理活跃 Session
class SessionPool {
public:
    /// 获取或创建 Session
    std::shared_ptr<workspace::Session> get_or_create(
        const container::String& session_id,
        agent::SharedResources& resources
    ) {
        std::string sid(session_id.c_str());

        {
            std::shared_lock lock(mutex_);
            auto it = sessions_.find(sid);
            if (it != sessions_.end()) {
                return it->second;
            }
        }

        std::unique_lock lock(mutex_);
        auto it = sessions_.find(sid);
        if (it != sessions_.end()) {
            return it->second;
        }

        auto deps = resources.make_session_deps();
        auto session = std::make_shared<workspace::Session>(
            workspace::SessionConfig{session_id, resources.settings().context_length},
            std::move(deps),
            resources.tools_mut()
        );

        // 恢复历史
        if (!session_id.empty()) {
            session->restore_from_db(resources.history_db());
        }

        sessions_[sid] = session;
        log::info_fmt("session pool: created session={}", sid);
        return session;
    }

    /// 归档空闲 Session（释放独占资源）
    void archive_idle(std::chrono::seconds max_idle) {
        // 遍历 last_access_，超时的 Session 从 sessions_ 移除
        // Session 析构自动释放 EventLoop、Compactor 等
    }

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<workspace::Session>> sessions_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> last_access_;
};
```

### 5.5 SSE 流式适配器

```cpp
// server/sse_writer.hpp

/// SSE 事件写入器（将 Agent 回调转换为 SSE 事件）
class SSEWriter {
public:
    explicit SSEWriter(net::TcpStream& stream)
        : stream_(stream) {}

    /// 写入 SSE 事件
    void write_event(std::string_view event, const Json& data) {
        // event: <event_type>\ndata: <json>\n\n
        std::string payload;
        if (!event.empty()) {
            payload += "event: ";
            payload += event;
            payload += "\n";
        }
        payload += "data: ";
        payload += data.dump();
        payload += "\n\n";
        stream_.write_all(payload.data(), payload.size());
    }

    /// 写入 token 增量
    void write_token(std::string_view token) {
        write_event("message", Json{{"content", std::string(token)}});
    }

    /// 写入工具调用
    void write_tool_call(const llm::ToolCallRequest& call) {
        write_event("tool_call", Json{
            {"id", std::string(call.id.c_str())},
            {"name", std::string(call.name.c_str())},
            {"arguments", call.arguments}
        });
    }

    /// 写入完成
    void write_done() {
        write_event("done", Json{});
    }

private:
    net::TcpStream& stream_;
};
```

```cpp
// server/server_callbacks.hpp

/// Server 模式 Agent 回调（将事件推送到 SSE）
class ServerAgentCallbacks : public AgentCallbacks {
public:
    explicit ServerAgentCallbacks(SSEWriter& writer)
        : writer_(writer) {}

    void on_token(std::string_view token) const override {
        writer_.write_token(token);
    }

    void on_thinking(std::string_view token) const override {
        writer_.write_event("thinking", Json{{"content", std::string(token)}});
    }

    void on_tool_call(const ToolCallRequest& call) const override {
        writer_.write_tool_call(call);
    }

    void on_tool_result(const ToolCallResult& result) const override {
        writer_.write_event("tool_result", Json{
            {"id", std::string(result.tool_call_id.c_str())},
            {"success", result.success},
            {"output_size", result.output.size()}
        });
    }

private:
    SSEWriter& writer_;
};
```

### 5.6 请求处理流程

```
HTTP Request → 路由匹配 → 中间件 → Handler
                                  │
                                  ▼
                          ┌───────────────┐
                          │  AgentPool    │──→ get_or_create(user, ws)
                          └───────┬───────┘
                                  │ SharedResources
                                  ▼
                          ┌───────────────┐
                          │  SessionPool  │──→ get_or_create(session_id, resources)
                          └───────┬───────┘
                                  │ Session
                                  ▼
                          ┌───────────────┐
                          │  Agent        │──→ run_session_async(loop, session, ...)
                          └───────┬───────┘
                                  │
                          ┌───────┴───────┐
                          │  Callbacks    │
                          │  ├─ SSE 模式  │──→ SSEWriter → TcpStream → Client
                          │  └─ JSON 模式 │──→ 等待完成 → JSON Response
                          └───────────────┘
```

### 5.7 并发模型

```
                    ┌─────────────────────┐
                    │  EventLoop (per request) │
  Request A ──────▶│  co_await agent.run_session_async()  │
                    └─────────────────────┘

                    ┌─────────────────────┐
                    │  EventLoop (per request) │
  Request B ──────▶│  co_await agent.run_session_async()  │
                    └─────────────────────┘

                    ┌─────────────────────┐
                    │  core_pool_ (2-8 threads)  │
  Tool calls ─────▶│  ToolCallManager.submit()  │
                    └─────────────────────┘
```

- 每个 HTTP 请求创建独立 EventLoop（与 Session 独占模型一致）
- 工具调用共享 core_pool_（已有架构支持）
- Session 池确保同一 session_id 不会并发执行（需加锁或排队）

### 5.8 会话并发保护

```cpp
// server/session_lock.hpp

/// 会话级互斥锁（同一 session_id 的请求串行执行）
class SessionLockManager {
public:
    /// 获取会话锁（RAII）
    class SessionGuard {
    public:
        ~SessionGuard() {
            std::lock_guard lock(manager_.mutex_);
            manager_.locks_.erase(session_id_);
        }
    private:
        SessionLockManager& manager_;
        std::string session_id_;
    };

    /// 尝试获取锁（同一 session_id 已有请求时返回 nullopt）
    std::optional<SessionGuard> try_acquire(const std::string& session_id) {
        std::unique_lock lock(mutex_);
        if (locks_.count(session_id)) return std::nullopt;
        locks_.insert(session_id);
        return SessionGuard(*this, session_id);
    }

private:
    std::mutex mutex_;
    std::unordered_set<std::string> locks_;
};
```

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

```cpp
// main.cpp 新增 --server 模式
if (server_mode) {
    auto pool = std::make_shared<AgentPool>();
    auto session_pool = std::make_shared<SessionPool>();
    auto lock_mgr = std::make_shared<SessionLockManager>();

    HttpServer srv(config.server.host, config.server.port);
    srv.route("POST", "/v1/chat/completions", chat_handler(pool, session_pool, lock_mgr));
    srv.route("POST", "/v1/sessions", session_create_handler(pool));
    srv.route("GET",  "/v1/sessions", session_list_handler(pool));
    srv.route("DELETE", "/v1/sessions/{id}", session_delete_handler(pool));
    srv.route("GET",  "/v1/health", health_handler());
    srv.route("GET",  "/v1/metrics", metrics_handler(pool));

    log::info_fmt("BenGear server listening on {}:{}",
                  config.server.host, config.server.port);
    srv.run();  // 阻塞运行
}
```

### 5.11 实现步骤

| 步骤 | 内容 | 改动文件 |
|------|------|---------|
| 1 | 定义 `ServerSettings` | 修改 `config/settings.hpp` |
| 2 | 实现 `AgentPool` | 新建 `server/agent_pool.hpp` |
| 3 | 实现 `SessionPool` | 新建 `server/session_pool.hpp` |
| 4 | 实现 `SSEWriter` + `ServerCallbacks` | 新建 `server/sse_writer.hpp` |
| 5 | 实现 `SessionLockManager` | 新建 `server/session_lock.hpp` |
| 6 | 实现 HTTP 路由 + Handler | 新建 `server/router.hpp` |
| 7 | main.cpp 添加 `--server` 入口 | 修改 `main.cpp` |
| 8 | 配置文件支持 `server` 字段 | 修改 `config/loader.hpp` |
| 9 | 集成测试 | 新建 `tests/test_server.cpp` |
| 10 | 更新文档 | 修改 `docs/` |

---

## 6. 三种模式的共享与差异

### 6.1 组件共享矩阵

| 组件 | Single Agent | Multi-Agent | Server |
|------|:-----------:|:-----------:|:------:|
| SharedResources | 1 实例 | 1 实例（共享） | AgentPool 管理 N 实例 |
| Agent | 1 实例 | N 实例（Lead + Sub） | N 实例（按请求） |
| Session | 1 实例 | N 实例（各自独占） | SessionPool 管理 |
| ToolRegistry | 1 实例 | 1 实例（共享） | N 实例（随 SharedResources） |
| core_pool_ | 1 实例 | 1 实例（共享） | 1 全局实例 |
| WorkflowEngine | 1 实例 | 1 实例（共享） | 1 全局实例 |
| EventLoop | 1 实例 | N 实例（随 Session） | N 实例（随请求） |

### 6.2 I/O 适配层差异

| 模式 | 输入 | 输出 | 回调实现 |
|------|------|------|---------|
| Single Agent | stdin | stdout/stderr | `TerminalAgentCallbacks` |
| Multi-Agent | tool args | tool return | `NullAgentCallbacks`（SubAgent） |
| Server | HTTP JSON | SSE / HTTP JSON | `ServerAgentCallbacks` |

### 6.3 启动方式

```bash
# 模式一：交互式（默认）
./bengear
./bengear --user alice --workspace project-x

# 模式二：Multi-Agent（自动，由 LLM 决定是否 spawn_agent）
./bengear  # 与模式一相同，LLM 可调用 spawn_agent 工具

# 模式三：Server
./bengear --server --port 8080
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
        "max_tool_steps": 50,
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
| 请求超时 | SSE 发送 `error` 事件 + 关闭连接 |
| Session 并发 | `SessionLockManager` 拒绝并发请求 |
| AgentPool OOM | `agent_pool_max_size` 限制 + LRU 淘汰 |
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
| `AgentPool.get_or_create` | 缓存命中/未命中 |
| `SessionPool.get_or_create` | Session 创建/恢复 |
| `SessionLockManager.try_acquire` | 并发保护 |
| `SSEWriter.write_event` | SSE 编码正确性 |

### 10.2 集成测试

| 测试 | 覆盖 |
|------|------|
| Lead Agent → spawn_agent → SubAgent | 完整调用链 |
| 并行 SubAgent → 结果汇总 | WorkflowEngine DAG |
| Server → /v1/chat/completions | HTTP + SSE |
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

### Phase 1：Multi-Agent 协作（P1）

- [ ] `SubAgentConfig` / `SubAgentResult` 类型定义
- [ ] `spawn_agent` 工具实现
- [ ] 嵌套深度控制
- [ ] 集成测试 + 端到端验证

**预计改动**：~500 行新增代码，无破坏性变更

### Phase 2：Server 基础版（P2）

- [ ] `AgentPool` + `SessionPool`
- [ ] HTTP 路由 + Handler
- [ ] SSE 流式输出
- [ ] 会话并发保护
- [ ] 配置支持

**预计改动**：~1500 行新增代码，无破坏性变更

### Phase 3：Server 增强（P3）

- [ ] 认证中间件
- [ ] 限流中间件
- [ ] 指标监控 `/v1/metrics`
- [ ] 优雅关闭
- [ ] Docker 镜像

**预计改动**：~800 行新增代码

---

## 12. 风险评估

| 风险 | 概率 | 影响 | 缓解措施 |
|------|------|------|---------|
| SubAgent 资源泄漏 | 中 | 高 | `max_agent_depth` + `max_tool_steps` 双重限制 |
| Session 并发冲突 | 低 | 高 | `SessionLockManager` 串行化 |
| Server 内存膨胀 | 中 | 中 | AgentPool LRU + `agent_pool_max_size` |
| SSE 连接断开 | 高 | 低 | 客户端自动重连 + Session 恢复 |
| ThreadPool 死锁 | 低 | 高 | 核心调度与 I/O 分离（已有设计） |

---

## 附录 A：目录结构（实施后）

```
include/ben_gear/
├── agent/
│   ├── agent.hpp              # 无状态调度器
│   ├── shared_resources.hpp   # 共享资源
│   ├── sub_agent.hpp          # 🆕 SubAgent 配置/结果
│   └── callbacks.hpp          # 回调接口
├── server/                    # 🆕 Server 模式
│   ├── agent_pool.hpp         # 🆕 Agent 池
│   ├── session_pool.hpp       # 🆕 Session 池
│   ├── session_lock.hpp       # 🆕 会话并发锁
│   ├── sse_writer.hpp         # 🆕 SSE 写入器
│   ├── router.hpp             # 🆕 HTTP 路由
│   └── server_callbacks.hpp   # 🆕 Server 回调
├── tools/
│   ├── agent_tools.hpp        # 🆕 spawn_agent 工具
│   ├── builtin_tools.hpp
│   ├── memory_tools.hpp
│   ├── skill_tools.hpp
│   ├── workflow_tools.hpp
│   └── workspace_tools.hpp
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
        "max_tool_steps": 50,
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
