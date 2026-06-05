# 架构设计

## 设计原则

### 高内聚
- 每个模块职责单一
- 相关功能聚合在一起
- 模块内部高度相关

### 低耦合
- 模块间通过接口交互
- 依赖注入设计（SharedResources 模式）
- 易于单元测试和替换

### 统一抽象
- 一套代码支持多个 LLM 提供商
- 统一的消息格式（ContentBlock）
- 统一的工具调用流程

### 可扩展
- 易于添加新工具
- 易于支持新 LLM 提供商
- 插件化架构

## 核心架构

### SharedResources 模式

BenGear 采用 SharedResources 模式管理共享资源。按 (用户, 工作空间) 构建一次，多 Agent / 多会话复用：

```cpp
class SharedResources {
public:
    explicit SharedResources(config::Settings settings, workspace::WorkspaceContext ws_ctx);

    // 所有 const 访问器线程安全
    const config::Settings& settings() const noexcept;
    const llm::ProviderClient& provider() const noexcept;
    const llm::ToolRegistry& tools() const noexcept;
    const skill::SkillLoader& skill_loader() const noexcept;
    const std::shared_ptr<memory::MemoryStore>& memory_store() const noexcept;
    const std::shared_ptr<memory::EpisodeStore>& episode_store() const noexcept;
    const std::unique_ptr<memory::ContextBuilder>& context_builder() const noexcept;
    session::HistoryDB& history_db() noexcept;           // 内部同步
    const std::unique_ptr<role::RoleLoader>& role_loader() const noexcept;
    const std::shared_ptr<workspace::WorkspaceManager>& workspace_manager() const noexcept;
    mcp::MCPManager& mcp_manager() noexcept;             // 内部同步
    const workspace::WorkspaceContext& workspace_context() const noexcept;
    int max_tool_steps() const noexcept;

    void register_tool(name, description, parameters, executor);  // ToolRegistry 内部 shared_mutex
};
```

**初始化流程**（`init()` 拆分为 7 个阶段）：
1. `init_workspace()` — 创建 WorkspaceManager
2. `init_memory()` — 创建 MemoryStore、EpisodeStore、ContextBuilder
3. `init_history()` — 创建 HistoryDB（SQLite）
4. `init_tools()` — 注册所有工具（内置 + 记忆 + 工作空间）
5. `init_skills()` — 发现技能（SkillLoader::discover + 内置技能）
6. `init_roles()` — 发现角色（RoleLoader::discover）
7. `init_mcp()` — 连接 MCP 服务器并注册 MCP 工具（`mcp_` 前缀）

### Session 隔离

每个 Session 独占以下资源，无需加锁：

- `ConversationHistory` — 对话历史
- `EventLoop` — 事件循环
- `Compactor` — 上下文压缩器（含持久化缓存）
- `MemoryUpdater` — LLM 记忆更新器

```cpp
class Session {
public:
    explicit Session(SessionConfig config, SessionDeps deps);

    llm::ConversationHistory& history();        // 独占
    net::EventLoop& event_loop();               // 独占

    void maybe_compact(EventLoop& loop, const ProviderClient& provider, const ToolRegistry& tools);
    void persist_message(role, content, HistoryDB& db);
    void persist_assistant_with_tools(content, tool_calls, HistoryDB& db);
    void persist_tool_result(tool_call_id, tool_name, content, HistoryDB& db);
    void restore_from_db(HistoryDB& db);
};
```

### Agent 无状态调度器

Agent 不持有对话状态，通过 `run_session_async` 接受 Session 引用执行对话：

```cpp
class Agent {
public:
    // 从 SharedResources 构造（多 Agent 共享资源）
    Agent(std::shared_ptr<SharedResources> resources, container::String role = "lead");

    // 从 Settings + WorkspaceContext 构造（向后兼容）
    Agent(config::Settings settings, workspace::WorkspaceContext ws_ctx);

    // 基于 Session 的异步聊天（线程安全，Session 独占 history）
    net::Task<ChatResult> run_session_async(net::EventLoop& loop,
                                            workspace::Session& session,
                                            container::String prompt,
                                            const AgentCallbacks& callbacks);

    // 角色过滤
    std::vector<ToolCallRequest> filter_tool_calls(const std::vector<ToolCallRequest>& calls);
};
```

**典型用法**：

```cpp
auto resources = std::make_shared<SharedResources>(settings, ws_ctx);
Agent agent(resources, "lead");

auto deps = resources->make_session_deps();
Session session(SessionConfig{session_id, context_length}, deps);
auto result = loop.run(agent.run_session_async(loop, session, "prompt", callbacks));
```

## 核心模块

### 1. Agent 层 (`ben_gear/agent/`)

**职责**：Agent 编排、工具管理和会话调度

**核心类**：
- `Agent` — 无状态调度器，不持有 ConversationHistory
- `AgentCallbacks` — 回调接口（`on_token`/`on_thinking`/`on_tool_call`/`on_tool_result`）
- `NullAgentCallbacks` — 空回调实现
- `SharedResources` — 共享只读/线程安全可变资源

**关键功能**：
- Session-based 对话管理（每个 Session 独占 history）
- 流式/非流式双路径（根据 `settings.stream` 自动选择）
- 流式增量工具调用解析（`StreamToolCallDelta` → `PendingToolCall`）
- 工具调用循环（`max_tool_steps` 限制）
- 角色过滤（ToolFilter 白名单）
- 记忆压缩（Compactor）
- LLM 记忆更新（MemoryUpdater）
- 持久化到 HistoryDB

### 2. LLM 层 (`ben_gear/llm/`)

**职责**：LLM 协议实现和工具调用

**核心模块**：

#### 客户端
- `provider_client.hpp` — 统一客户端接口（协议分发边界）
- `openai_client.hpp` — OpenAI 客户端
- `anthropic_client.hpp` — Anthropic 客户端

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

### 3. 工具层 (`ben_gear/tool/` + `ben_gear/tools/`)

**职责**：工具注册、管理和执行

**核心类**：
- `ToolRegistry` — 工具注册表（线程安全，shared_mutex）
- `ToolCallManager` — 工具调用管理器
- `ToolFilter` — 角色过滤（组合模式，白名单）

**工具分类**：
- 内置工具：文件（10 个）、命令（1 个）、HTTP（2 个）
- 技能工具：get_skill、install_skill、remove_skill、enable_skill、disable_skill、list_skills
- 记忆工具：read/write_memory、recall、read/write_soul、read/write_rules、append_episode
- 工作空间工具：list/create/remove/restore_workspace
- MCP 工具：自动发现，`mcp_` 前缀

### 4. 配置层 (`ben_gear/config/`)

**职责**：配置加载和管理

**核心功能**：
- JSON 配置解析
- `model_config` 分组格式（provider → models 列表）
- 多层配置合并
- 环境变量替换
- MCP 服务器配置
- 多级管理字段（username、workspace_name、session_id、role）

### 5. 技能层 (`ben_gear/skill/`)

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

### 6. MCP 层 (`ben_gear/mcp/`)

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

### 7. 记忆系统 (`ben_gear/memory/`)

**职责**：三层级记忆存储、上下文压缩和记忆更新

**核心类**：
- `MemoryStore` — 三层级记忆存储（跨进程文件锁 + 原子写入）
- `EpisodeStore` — 每日情景记忆（FileLock 安全追加）
- `ContextBuilder` — 系统提示组装器 + CJK 感知 token 估算
- `Compactor` — 上下文压缩器（软/硬阈值，持久化缓存）
- `MemoryUpdater` — LLM 记忆更新器（重试 + 标签提取）
- `merge_sections()` — 三层级 section 合并算法（last-wins）

### 8. 工作空间 (`ben_gear/workspace/`)

**职责**：多用户多工作空间管理

**核心类**：
- `WorkspaceManager` — CRUD + 软删除/恢复 + 默认模板
- `Session` — 会话类（独占 history + Compactor + MemoryUpdater）
- `TierPaths` — 三层级路径集合
- `WorkspaceContext` — 传递给 Agent/Session 的上下文

### 9. 角色系统 (`ben_gear/role/`)

**职责**：基于白名单的工具过滤

**核心类**：
- `RoleDefinition` — 角色定义（name、description、tool_whitelist）
- `RoleLoader` — 三层级角色加载器
- `ToolFilter` — 组合模式过滤器（不过滤/白名单过滤）

### 10. 网络层 (`ben_gear/base/net/`)

**职责**：网络通信

**核心模块**：
- `http.hpp` — HTTP 客户端（内置连接池 + ObjectPool）
- `connection_pool.hpp` — 连接池（预热 + shared_mutex 读写锁）
- `event_loop.hpp` — 事件循环
- `socket.hpp` — Socket 封装
- `task.hpp` — 协程任务
- `tcp_stream.hpp` — TCP 流

### 11. 日志层 (`ben_gear/base/log/`)

**职责**：异步日志

**核心模块**：
- `logger.hpp` — 日志记录器（前端轻量采集 + 后端异步格式化）
- `sink.hpp` — 输出目标（Stdout / File 轮转 / TCP Server）
- `level.hpp` — 日志级别
- `configure.hpp` — 日志配置

## 工作流程

```text
用户输入
  → Agent.run_session_async
  → Session.history 追加 user 消息
  → 调用 LLM (带工具定义 + 角色过滤)
    ├─ 流式：StreamHandlers 增量解析 token + thinking + tool_call
    └─ 非流式：完整响应解析
  → LLM 返回工具调用请求
  → ToolFilter 过滤（角色白名单）
  → ToolManager 执行工具
  → 构建工具结果消息
  → 持久化到 HistoryDB
  → Maybe Compact（Compactor 压缩旧轮次）
    ├─ 软/硬双阈值检测
    ├─ 批量摘要旧轮次
    └─ 持久化缓存
  → Maybe MemoryUpdate（MemoryUpdater 更新长期记忆和情景）
  → 再次调用 LLM (循环)
  → 返回最终结果
```

## 系统提示组装

ContextBuilder 按 7 步组装系统提示：

1. **SOUL.md** — 身份定义（三层级合并）
2. **核心提示** — 自定义或默认 "You are BenGear..."
3. **RULES.md** — 行为规范（三层级合并）
4. **技能列表** — SkillLoader Level 1 元数据
5. **MEMORY.md** — 长期记忆（三层级合并，跳过空记忆）
6. **工作空间信息** — 项目路径
7. **AGENTS.md / CLAUDE.md** — 项目文档（自动发现）

`exclude_character=true` 时跳过 SOUL/core/RULES（用于 teammate 角色）。

## 关键设计模式

### 1. 适配器模式

**应用场景**：协议适配

```cpp
// OpenAI 适配器
Json to_openai_format() const;
static ToolCallRequest from_openai(const Json& j);

// Anthropic 适配器
Json to_anthropic_format() const;
static ToolCallRequest from_anthropic(const Json& j);
```

**优势**：统一抽象、易于扩展新协议、隔离协议细节

### 2. 策略模式

**应用场景**：工具执行

```cpp
using ToolExecutor = std::function<container::String(const Json& arguments)>;
registry.register_tool(name, description, parameters, executor);
```

**优势**：工具实现灵活、易于测试、运行时可配置

### 3. 观察者模式

**应用场景**：回调通知

```cpp
class AgentCallbacks {
public:
    virtual void on_token(std::string_view token) const {}
    virtual void on_thinking(std::string_view token) const {}
    virtual void on_tool_call(const ToolCallRequest& call) const {}
    virtual void on_tool_result(const ToolCallResult& result) const {}
};
```

**优势**：解耦事件生成和处理、灵活的订阅机制、易于扩展

### 4. 工厂模式

**应用场景**：SharedResources 初始化

```cpp
void SharedResources::init() {
    // 创建并注册所有组件
    ws_manager_ = std::make_shared<WorkspaceManager>(...);
    memory_store_ = std::make_shared<MemoryStore>(...);
    tools::register_all_tools(tools_, ...);
    tools::register_memory_tools(tools_, ...);
    tools::register_workspace_tools(tools_, ...);
    skill_loader_.discover();
    role_loader_->discover();
    // MCP 工具注册...
}
```

**优势**：集中创建逻辑、易于维护、支持自定义

### 5. 组合模式

**应用场景**：角色工具过滤

```cpp
class ToolFilter {
public:
    explicit ToolFilter(const container::Vector<container::String>& whitelist);
    bool is_allowed(std::string_view tool_name) const;
    Json to_openai_tools(const ToolRegistry& registry) const;
    Json to_anthropic_tools(const ToolRegistry& registry) const;
    std::shared_ptr<ToolRegistry> filtered_registry(const ToolRegistry& registry) const;
};
```

**优势**：不修改原 Registry、安全无侵入、白名单空=不过滤

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

### 5. 响应超时保护

```cpp
// EventLoop::close_after 在超时时关闭 fd 并唤醒挂起的 I/O 协程
// 防止 LLM 服务端无响应时永久挂起
const auto timeout = pool->config().response_timeout; // 默认 60s
loop->close_after(fd, timeout);
```

异常类型 `ResponseTimeoutError` 继承 `std::runtime_error`，不会被 HTTP 重试逻辑重试。

### 6. 共享工具线程池

```cpp
// SharedResources 持有共享线程池，多 Agent 复用
auto tool_pool = std::make_shared<ThreadPool>(config);
ToolCallManager manager(registry, tool_pool, timeout);
```

### 4. CJK 感知 token 估算

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

### 4. MemoryUpdater 重试

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

```cpp
class NewProviderClient {
public:
    Json chat_with_tools_async(EventLoop& loop, const ConversationHistory& history, const ToolRegistry& tools);
    static ToolCallRequest from_provider_format(const Json& j);
    Json to_provider_format() const;
};
```

### 2. 新增工具

```cpp
agent.register_tool("custom_tool", "描述", { /* 参数 */ }, [](const Json& args) { /* 实现 */ });
```

### 3. 自定义回调

```cpp
class MyCallbacks : public AgentCallbacks {
    void on_tool_call(const ToolCallRequest& call) const override { /* 自定义处理 */ }
};
```

### 4. 新增角色

在工作空间的 `roles/` 目录创建 JSON 文件：

```json
{"name": "reviewer", "description": "Read-only agent", "tool_whitelist": ["read_file", "list_directory", "get_skill"]}
```

## 未来规划

### 短期
- [x] 流式工具调用（增量解析）
- [x] 工具调用超时控制
- [x] MCP HTTP 传输支持
- [x] 记忆系统（MemoryStore、EpisodeStore、Compactor、MemoryUpdater）
- [x] 工作空间管理（WorkspaceManager、Session）
- [x] 角色过滤（RoleLoader、ToolFilter）
- [x] SharedResources 共享资源模式
- [x] 安全子进程（fork+execvp）
- [x] 跨进程文件锁

### 中期
- [ ] 多 Agent 协作
- [ ] 技能市场
- [ ] Web UI

### 长期
- [ ] 插件系统
- [ ] 分布式部署
- [ ] 模型微调集成
