# BenGear 多级管理实施计划

## 概述

为 BenGear 实现多级管理（全局 → 用户 → 工作空间）、结构化记忆系统、角色机制、上下文压缩和线程安全隔离。参考 yzx_agent 项目设计，核心约束：**多线程、多会话、多 Agent 安全隔离，互不影响**。

---

## 已完成

- [x] Phase 1: 基础类型（uuid, TierPaths, MemoryKind, RoleDefinition）
- [x] Phase 2: 核心算法（section_merge, HistoryDB/SQLite, ContextBuilder）
- [x] Phase 3: 管理器（WorkspaceManager, Session, MemoryStore, EpisodeStore, RoleLoader, ToolFilter）
- [x] Phase 4: 上下文压缩与记忆更新（Compactor, MemoryUpdater）
- [x] Phase 6: 集成现有代码（ToolRegistry 安全修复, Settings 扩展, Loader 7 层覆盖, SkillLoader 3 层级, Agent 重构, 记忆/工作空间工具）
- [x] Phase 7: 构建依赖（SQLite3 vendored, CMake 集成, 编译通过, 测试通过）
- [x] MCPManager 锁粒度优化：execute_tool() 先查找后释放锁再 I/O
- [x] Compactor 集成到 Session：Agent 构造函数创建 compactor_/memory_updater_
- [x] MemoryUpdater 集成：压缩后自动触发 LLM 记忆更新
- [x] Session 恢复：restore_from_db 完善 system/tool 消息处理 + persist_tool_result
- [x] CLI 扩展：--user/--workspace-name/--session/--role/--new-session 标志
- [x] 工作空间子命令（workspace list/create/remove/restore）
- [x] 会话子命令（session list/delete）
- [x] apply_values 支持新字段（username, workspace_name, role, session_id）
- [x] Agent 使用 build_ws_ctx() 构造 WorkspaceContext
- [x] 测试框架重构：gtest/glog 源码依赖 + 76 个 gtest 用例模块化拆分
- [x] Bug 修复：角色过滤器实际生效、压缩器缓存淘汰逻辑、localtime_r 线程安全、async_connect SO_ERROR 检查、on_token 重复回调、SSE forward 修复、OpenAI 客户端重试、SQLite NULL 检查、EpisodeStore RAII + 完整写入、Store fsync 原子写入、parse_bool 修复、MemoryPool::reset() 实现、工作区目录遍历防护、MCP HTTP transport 实现
- [x] HistoryDB 读写锁优化：std::mutex → shared_mutex，读操作用 shared_lock 允许并发

---

## 待完成

### 线程安全加固

- [x] MCPClient 并发执行：同 server 串行、不同 server 并行的 MVP 方案
- [x] ConnectionPool 实际启用：`HttpClient::request_async` 使用 acquire/release + keep-alive
- [x] ToolCallManager 并行执行：支持同一轮多个工具并行执行
- [x] 编译 warning 清理：unused parameter、初始化顺序、未使用字段

### Agent 集成完善

- [x] Agent `system_prompt()` 通过 ContextBuilder 组装时注入 SOUL/RULES/MEMORY
- [x] Agent 构造函数统一为 `Agent(Settings, WorkspaceContext)`，删除旧兼容接口
- [x] 会话恢复：`Session::restore_from_db` 完善 assistant+tool 消息块（content blocks）还原

### CLI 扩展

- [x] 初始化流程完善：--session 恢复时实际调用 restore_from_db

### 记忆系统完善

- [x] 记忆工具 `write_memory` 支持 tier 参数选择
- [x] `recall` 工具支持 section 级别搜索（当前仅行级关键词）
- [x] SOUL.md / RULES.md 写入工具（当前只有读取）
- [x] EpisodeStore 日期范围查询修复（YYYYMMDD → YYYY-MM-DD 格式转换）

### 配置系统

- [x] 用户级 .conf 和工作空间级 .conf 的实际测试
- [ ] 环境变量 BEN_GEAR_USER / BEN_GEAR_WORKSPACE / BEN_GEAR_ROLE 集成测试

### 测试

- [x] section_merge 单元测试：空输入、单层、多层覆盖、同名 last-wins、插入顺序保持
- [x] uuid 单元测试：格式验证、唯一性
- [x] WorkspaceManager 单元测试：创建/列表/删除/恢复/默认模板
- [x] MemoryStore 单元测试：读写各层级、merge 正确性、空文件处理
- [x] HistoryDB 单元测试：append/load/list/delete/search
- [x] ToolFilter 单元测试：空白名单不过滤、非空正确过滤
- [x] RoleLoader 单元测试：加载和覆盖、whitelist 解析
- [x] 线程安全测试：多线程并发 ToolRegistry::execute + unregister
- [x] 线程安全测试：多 Session 并发调用 HistoryDB
- [x] ThreadSanitizer 验证：`-fsanitize=thread` 无数据竞争

### 多 Agent 协作（暂缓）

- [ ] MessageBus（JSONL 文件消息总线）
- [ ] Blackboard（共享 KV 存储 + 条件等待）
- [ ] TaskGraph（DAG 任务图 + 安全条件求值）
- [ ] Orchestrator（DAG 调度器）
- [ ] TeammateManager（队友线程管理）
- [ ] 团队工具（send_message, blackboard_read/write, dispatch_workflow）

---

## 线程安全矩阵

| 资源 | 所属层 | 访问模式 | 保护机制 |
|------|--------|----------|----------|
| ConversationHistory | Session 独占 | 单线程 | 无需加锁 |
| EventLoop | Session 独占 | 单线程 | 无需加锁 |
| Settings | Agent 只读 | 多线程读 | 构造后不变 |
| ToolRegistry | Agent 共享 | 多线程读写 | shared_mutex + 拷贝 executor |
| SkillLoader | Agent 共享 | 多线程读 | shared_mutex |
| MemoryStore | Agent 共享 | 多线程读写 | 文件锁 |
| HistoryDB | Agent 共享 | 多线程读写 | shared_mutex（读共享/写独占） |
| MCPManager | Agent 共享 | 多线程读写 | shared_mutex |
| RoleLoader | Agent 只读 | 多线程读 | shared_mutex |
| ToolFilter | Agent 只读 | 多线程读 | 构造后不变 |

---

## 关键文件索引

### 新增文件
- `include/ben_gear/session/uuid.hpp` — UUID v4
- `include/ben_gear/session/history_db.hpp` — SQLite pimpl 封装
- `src/session/history_db.cpp` — SQLite 实现
- `include/ben_gear/workspace/types.hpp` — TierPaths, WorkspaceMeta, WorkspaceContext
- `include/ben_gear/workspace/manager.hpp` — WorkspaceManager
- `include/ben_gear/workspace/session.hpp` — Session 隔离单元
- `include/ben_gear/memory/types.hpp` — MemoryKind, MergedMemory
- `include/ben_gear/memory/store.hpp` — MemoryStore 三层级 merge
- `include/ben_gear/memory/episode.hpp` — EpisodeStore 每日情景
- `include/ben_gear/memory/section_merge.hpp` — merge_sections() 算法
- `include/ben_gear/memory/context.hpp` — ContextBuilder + token 估算
- `include/ben_gear/memory/compactor.hpp` — Compactor 上下文压缩
- `include/ben_gear/memory/updater.hpp` — MemoryUpdater LLM 记忆更新
- `include/ben_gear/role/types.hpp` — RoleDefinition
- `include/ben_gear/role/loader.hpp` — RoleLoader 三层级扫描
- `include/ben_gear/role/filter.hpp` — ToolFilter 白名单过滤
- `include/ben_gear/tools/memory_tools.hpp` — 记忆工具（6 个）
- `include/ben_gear/tools/workspace_tools.hpp` — 工作空间工具（4 个）
- `third_party/sqlite3/sqlite3.c` / `sqlite3.h` — vendored SQLite

### 测试文件
- `tests/test_main.cpp` — gtest + glog runner
- `tests/test_util.hpp` — TmpDirTest fixture
- `tests/test_base_utils.cpp` — String/JSON 工具测试
- `tests/test_llm_clients.cpp` — OpenAI/Anthropic 客户端测试
- `tests/test_llm_stream.cpp` — 流式解析器测试
- `tests/test_llm_retry.cpp` — LLM 重试逻辑测试
- `tests/test_llm_endpoint.cpp` — 端点 URL 参数化测试
- `tests/test_config.cpp` — 配置加载 & 模型配置测试
- `tests/test_net.cpp` — 协程 & 事件循环测试
- `tests/test_session.cpp` — UUID & HistoryDB 测试
- `tests/test_memory.cpp` — section_merge & MemoryStore 测试
- `tests/test_memory_episode.cpp` — EpisodeStore & Compactor 测试
- `tests/test_workspace.cpp` — WorkspaceManager 测试
- `tests/test_tool.cpp` — 内置工具测试
- `tests/test_role.cpp` — ToolFilter & RoleDefinition 测试

### 第三方依赖（vendored）
- `third_party/googletest/` — Google Test v1.14.0 + Google Mock
- `third_party/glog/` — Google Log v0.7.1
- `third_party/sqlite3/` — SQLite3
- `third_party/nlohmann/` — nlohmann/json

### 修改文件
- `include/ben_gear/agent/agent.hpp` — Agent 重构为无状态调度器
- `include/ben_gear/tool/registry.hpp` — execute 拷贝 executor + for_each
- `include/ben_gear/config/settings.hpp` — 新增 username/workspace_name/session_id/role
- `include/ben_gear/config/loader.hpp` — 7 层 .conf + os::getenv_optional
- `include/ben_gear/skill/skill.hpp` — 3 层级构造 + TierPaths 工厂
- `CMakeLists.txt` — SQLite3 + C 语言 + history_db.cpp
