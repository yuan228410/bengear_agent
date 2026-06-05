# 计划：提取 SharedResources 支持多用户并发

## 背景

当前 `Agent` 是一个单体类，持有所有资源。这导致三个问题：
1. **Compactor/MemoryUpdater 是会话级状态却放在 Agent 中** — 多会话并发时逻辑状态会交叉污染
2. **重量级资源重复创建** — 每个 Agent 实例都创建独立的 ProviderClient、ToolRegistry、MCPManager、HistoryDB 等，浪费内存和初始化时间
3. **多 Agent 协作无共享机制** — 不同角色的 Agent 需要共享 ProviderClient、ToolRegistry 等但只能各自复制

目标：支持 CLI 多会话并发、Server 模式、多 Agent 协作三种场景。

## 核心改动

### 1. 新建 `SharedResources` 类

**文件**: `include/ben_gear/agent/shared_resources.hpp`

从 `Agent::init_from_ws_ctx()` 提取步骤 1-7 的初始化逻辑和对应成员。持有所有线程安全或昂贵的共享资源：

```
SharedResources(Settings, WorkspaceContext)
├── settings_           (不可变)
├── provider_           (所有方法 const，安全共享)
├── tools_              (shared_mutex 保护)
├── skill_loader_       (shared_mutex 保护)
├── memory_store_       (文件锁保护)
├── episode_store_      (文件锁保护)
├── context_builder_    (构造后不变)
├── history_db_         (pimpl+mutex)
├── role_loader_        (构造后只读)
├── ws_manager_         (文件系统级序列化)
├── mcp_manager_        (shared_mutex，I/O 前释放锁)
├── ws_ctx_             (不可变)
└── max_tool_steps_     (不可变)
```

提供 `const&` 或 `&` 访问器。`register_tool()` 委托给 `ToolRegistry`（线程安全）。

### 2. `Session` 接管 Compactor 和 MemoryUpdater

**文件**: `include/ben_gear/workspace/session.hpp`

- 新增成员: `compactor_`、`memory_updater_`
- 新增构造参数: `const EpisodeStore&`、`const ContextBuilder&`、`int64_t context_length`
- 新增方法: `maybe_compact(EventLoop&, const ProviderClient&, const ToolRegistry&)`
- Agent 的 `maybe_compact` 改为调用 `session.maybe_compact(loop, provider, tools)`

这是对已有 bug 的修复 — Compactor 的 `cached_summaries_` 和 `last_round_count_` 本就是会话级状态。

### 3. `Agent` 持有 `shared_ptr<SharedResources>`

**文件**: `include/ben_gear/agent/agent.hpp`

```
Agent 变为轻量调度器：
├── resources_          (shared_ptr<SharedResources>)
├── tool_filter_        (per-role，不同角色不同白名单)
├── tool_manager_       (per-agent，持有 timeout 配置)
└── enable_memory_      (atomic bool)
```

- 新增构造函数: `Agent(shared_ptr<SharedResources>, String role)`
- 保留旧构造函数: 内部创建 SharedResources（向后兼容）
- 所有内部方法从 `member_` 改为 `resources_->member()`
- 删除已移到 SharedResources 或 Session 的成员

### 4. 更新调用点

- `src/main.cpp` — Session 构造传入额外参数
- `examples/agent_cli.cpp` — 同上
- CMakeLists.txt — 添加新头文件

### 5. 三种场景使用方式

**CLI 多会话并发:**
```cpp
auto resources = make_shared<SharedResources>(settings, ws_ctx);
Agent agent(resources, "lead");
// 多个 Session 共享同一个 Agent/SharedResources
auto s1 = Session(sid1, ws_ctx, ...);
auto s2 = Session(sid2, ws_ctx, ...);
```

**Server 模式:**
```cpp
// 启动时按 (user, workspace) 缓存 SharedResources
auto resources = get_or_create(user, workspace);
Agent agent(resources, role);
auto session = get_or_create_session(resources, session_id);
agent.run_session_async(loop, session, prompt, callbacks);
```

**多 Agent 协作:**
```cpp
auto resources = make_shared<SharedResources>(settings, ws_ctx);
auto lead = make_shared<Agent>(resources, "lead");
auto coder = make_shared<Agent>(resources, "teammate");
// 共享 Provider/Registry/MCP，各自有不同的 ToolFilter
```

## 需要修改的文件

| 文件 | 改动 |
|------|------|
| `include/ben_gear/agent/shared_resources.hpp` | **新建** — SharedResources 类 |
| `include/ben_gear/agent/agent.hpp` | 重构 — 持有 shared_ptr，删除已提取成员 |
| `include/ben_gear/workspace/session.hpp` | 新增 compactor_/memory_updater_，maybe_compact 方法 |
| `src/main.cpp` | 更新 Session 构造参数 |
| `examples/agent_cli.cpp` | 更新 Session 构造参数 |

## 不变的文件

ToolRegistry、SkillLoader、HistoryDB、MCPManager、MemoryStore、ProviderClient、ToolCallManager、ToolFilter、Compactor、MemoryUpdater 的内部实现均不变 — 只是所有权和存放位置改变。

## 验证方式

```bash
cmake -S . -B build -DBEN_GEAR_BUILD_TESTS=ON
cmake --build build
./build/bengear_tests
```

所有 229 个测试应通过。可额外添加 SharedResources 构造测试和多 Agent 共享测试。
