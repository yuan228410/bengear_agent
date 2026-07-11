# Changelog

## [2026-06-27] Phase 3: Code Intelligence + Web Workbench

### Added

- **Repo Intelligence in Safe Code Change**
  - `SafeCodeChangeService` now accepts optional `code_intel::CodeIntelligenceIndex`
  - `SafeCodeChangeResult.repo_intelligence` populated before applying changes
  - Contains affected_paths, symbols, impacts, related_tests, and test_suggestions
  - Server composition injects code intelligence via `WorkspaceApplicationServices`

- **Enhanced Workbench Snapshot**
  - Unified repo map, code intel, and safe change integration
  - Request-scoped `CodeIntelligenceIndex` shares index across queries
  - Structured contexts: navigation, symbol, dependency, impact, readiness, gate, handoff

- **Documentation**
  - Comprehensive `docs/code_intel_workbench.md` covering:
    - API contracts for `/api/workbench/snapshot` and `/api/patch/safe-change`
    - Data flow diagrams
    - Implementation details
    - Extension points for future LSP provider

### Changed

- `SafeCodeChangeService` constructor now accepts optional `code_intelligence` parameter
- `make_patch_api_service` uses `WorkspaceApplicationServices` to inject dependencies
- `command_api_composition.cpp` includes `application_services.hpp`

### Verified

- All tests pass (bengear_tests)
- Build succeeds with no warnings
- Code intelligence queries work with shared request-scoped index

## [2026-07-12] T6–T10 架构重构：LLM 解耦 / 统一 Capability / Plugin Loader

### Added

- **ProviderRegistry 单例 + 静态 registrar** (`src/llm/provider_registry.hpp`)
  - 消除 `provider_client.cpp` 中硬编码 if/else 分发
  - 内置 Anthropic / OpenAI 通过 `BEN_GEAR_REGISTER_PROVIDER` 自动注册
  - 新增提供商仅需实现 `ProviderFactory` + `BEN_GEAR_REGISTER_PROVIDER`，无需修改分发逻辑

- **Capability 抽象层** (`src/capabilities/capability.hpp/.cpp`, `capability_registry.hpp`)
  - `ICapability` + `CapabilityBase<T>` CRTP 基类，统一 `name()` / `workspace_context()`
  - `CapabilityRegistry` 单例 + `CapabilityRegistrar` 静态注册宏 `BEN_GEAR_REGISTER_CAPABILITY`
  - 内置迁移：`GitService("git")`、`TestLoopService("test_loop")`、`PatchService("patch")`

- **Plugin Loader** (`src/plugins/plugin_loader.hpp/.cpp`)
  - `PluginLoader` 扫描目录加载 `.dll`/`.so`，调用 `ben_gear_plugin_init()`
  - 插件导出 `extern "C" void ben_gear_plugin_init()` 内部调用注册宏
  - 运行时动态加载能力/Provider，无需重新编译核心

- **LLM 层解耦** (`provider_client.hpp/.cpp`)
  - 移除对 `tool/registry.hpp`、`tool/types.hpp`、`workspace/conversation_history.hpp` 直接包含
  - 前向声明 `workspace::ConversationHistory`，完整类型仅在 `.cpp` 使用
  - `tool/types.hpp` 中的 `ToolChoiceConfig` 本就属于 `ben_gear::llm` 命名空间，保留包含

- **Logger macOS 兼容性修复**
  - `std::atomic<std::shared_ptr<Logger>>` 在 libc++ 上不满足 trivially copyable
  - 改为 `thread_local` 缓存 + `epoch` 原子计数器，`set_logger()` 增加 epoch 失效快速路径
  - 热路径零锁，冷路径仅 `set_logger()` 时获取 mutex

### Changed

- **ben_gear.hpp 裁剪**：仅保留 `agent/agent.hpp`、`base/config/loader.hpp`、`net/event_loop.hpp` 三大公共入口；CLI 实现文件补齐显式 include
- **ProviderClient 拆分**：`chat_with_tools_async` / `chat_stream_with_tools_async` / `make_client_fns` 定义移至 `provider_client.cpp`，头文件仅保留声明
- **Capability 服务统一基类**：`GitService`、`TestLoopService`、`PatchService` 继承 `CapabilityBase<T>`，显式构造函数传递 `WorkspaceContext`

### Fixed

- macOS (libc++) 编译报错 `std::atomic<std::shared_ptr<Logger>>` not trivially copyable
- `ChangeStore` 默认构造缺失导致的 `PatchService` 编译错误
- `GitService` / `TestLoopService` 重复构造函数定义

### Verified

- Build: g++ 16.1 (w64devkit) + Ninja + CMake 3.28 ✓
- Tests: 27 baseline failures (git/test_loop/repo_map env-dependent), no new regressions ✓
- macOS cross-check: clang 17 + libc++ ✓

## [2026-06-27] Phase 3: Code Intelligence + Web Workbench

### Added

- **Repo Intelligence in Safe Code Change**
  - `SafeCodeChangeService` now accepts optional `code_intel::CodeIntelligenceIndex`
  - `SafeCodeChangeResult.repo_intelligence` populated before applying changes
  - Contains affected_paths, symbols, impacts, related_tests, and test_suggestions
  - Server composition injects code intelligence via `WorkspaceApplicationServices`

- **Enhanced Workbench Snapshot**
  - Unified repo map, code intel, and safe change integration
  - Request-scoped `CodeIntelligenceIndex` shares index across queries
  - Structured contexts: navigation, symbol, dependency, impact, readiness, gate, handoff

- **Documentation**
  - Comprehensive `docs/code_intel_workbench.md` covering:
    - API contracts for `/api/workbench/snapshot` and `/api/patch/safe-change`
    - Data flow diagrams
    - Implementation details
    - Extension points for future LSP provider

### Changed

- `SafeCodeChangeService` constructor now accepts optional `code_intelligence` parameter
- `make_patch_api_service` uses `WorkspaceApplicationServices` to inject dependencies
- `command_api_composition.cpp` includes `application_services.hpp`

### Verified

- All tests pass (bengear_tests)
- Build succeeds with no warnings
- Code intelligence queries work with shared request-scoped index

## [2026-06-27] Phase 2: Safe Code Change Loop

### Added

- **Safe Code Change Service**
  - `SafeCodeChangeService` orchestrates patch, permission, checkpoint, git, and test loop
  - Server API: `POST /api/patch/safe-change`
  - Returns structured `SafeCodeChangeResult` with preview, checkpoint, patch_apply, git_status, git_diff, test_run
  - Failure paths provide rollback hints

- **Command Governance**
  - Permission gate before any write operation
  - Runtime event tracking through execution stages

- **Documentation**
  - `docs/safe_code_change_loop.md` describes the closed-loop flow

### Verified

- All tests pass
- Safe change flow validated with mock services

## [2026-06-27] Phase 1: Core / Runtime / UI Layer

### Added

- **Runtime Execution Kernel**
  - `core::RuntimeExecutionKernel` emits `core::RuntimeEvent`
  - CLI has `RuntimePresenter` to display execution progress

- **UI Layer Separation**
  - Core emits events, UI consumes
  - No business state in UI components

### Verified

- All tests pass
- Runtime events flow through presenter

## Unreleased

### 新增

- **Orchestration 领域层**：新增计划、TODO、执行事件等结构化领域模型与序列化能力。
- **Web 计划模式**：支持计划草稿、方案选择、自然语言修订、条目编辑、revision 校验和确认执行。
- **执行 TODO 面板**：按 workspace/session 隔离 TODO 状态，支持持久化、delta 更新、停止/重启后的继续执行上下文。
- **执行事件可视化**：统一展示 sub-agent、workflow、task、tool、approval 等执行事件。
- **Web 交互优化**：右侧可折叠 TODO 面板、工具调用分组折叠、滚动跟随控制、居中回到底部按钮、消息时间展示。
- **亮色主题**：新增 linen、paper、sage、porcelain 等参考项目亮色主题。
- **Server 文档**：新增 Server 模式与 Web 计划/TODO 文档，清理阶段性过程文档。

### 变更

- WebSocket 协议保持 `WsMessage v1`，通过结构化 `plan_*`、`todo_*`、`execution_event` 消息扩展能力。
- TODO 不再由普通工具调用自动生成，避免 `read_file`、`list_directory` 等工具污染任务列表。
- 手动停止、超时、工具限制或后端重启后只终结 running TODO，保留 pending/blocked 项用于继续执行。
- 系统提示词强调复杂任务由 LLM 自主决定是否拆解 TODO，后端不做启发式预处理或额外 preflight LLM 调用。
- Web UI 去除卡片/弹窗/工具块阴影和厚重强调线，改用轻边框与低对比背景。

## 0.2.0 (2026-06-11)

### 新增

- **TLS 抽象层**：`TlsEngine` 接口支持 MbedTLS / OpenSSL / Schannel / none 四后端，CMake `TLS_BACKEND` 编译期选择，`set_global_tls_engine()` 运行时替换
- **压缩抽象层**：`CompressEngine` 接口支持 zlib / none 后端，CMake `COMPRESS_BACKEND` 选择
- **自研轻量测试框架**：`ben_gear/test/test_framework.hpp`，gtest 宏兼容，零外部依赖
- **增量裁剪优化**：`ContextPruner` 冻结区跳过 + 活跃区重算，长对话场景 ~9× 加速
- **历史删除工具**：`delete_history` LLM 工具，支持按条件（全部/时间段/关键词/指定会话/消息级）删除历史记录，两步确认机制
- **REPL `/history delete`**：交互式删除历史（all/before/after/keyword/session/messages），y/N 确认
- **CLI 扩展**：`bengear session delete --all/--before/--after/--keyword` 条件删除，`--confirm` 跳过交互
- **`container::String` 拼接**：新增 `operator+` 和 `operator+=` 支持 `std::string` / `std::string_view` 互操作
- **Token 缓存**：`ConversationHistory::pruned_tokens()` / `original_tokens()`，增量维护 + 懒计算，消除 5×O(n) 重复扫描

### 变更

- `ContextPruner::prune()` 返回 `PruneResult{messages, hard_pruned, soft_pruned}`，不再内含 `estimate_tokens` 调用
- `ContextPruner` 新增 `compute_depths()` 和 `prune_range_with_depths()` 公共接口
- `ConversationHistory::add_message()` 增量维护 `original_tokens_`
- `Compactor::should_compact_local()` 改用 `history.pruned_tokens()`（更准确，零额外开销）
- `ConversationHistory::invalidate_cache()` / `invalidate_all_cache()` 分两级失效
- 连接池 TLS 类型安全：`void*` + 手动 `SSL_free()` → `unique_ptr<TlsEngine::Session>` RAII
- `HttpClient::Transport` 移除裸 `SSL*`/`SSL_CTX*`，改用 `TlsEngine::Session`
- `zip_extract.cpp` 改用 `global_compress_engine().inflate()`，不再直接 `#include <zlib.h>`
- 测试全部迁移至自研框架，移除 gtest/gmock/glog 第三方源码
- JSON 注释清理：移除 "nlohmann 兼容" 表述，标记为自研实现

### 移除

- `third_party/googletest/`、`third_party/glog/`、`third_party/nlohmann/json.hpp`

### 新增第三方

- `third_party/mbedtls/`（TLS 默认后端，vendor）
- `third_party/zlib/`（压缩后端，vendor）

### 性能

| 场景 | 优化前 | 优化后 | 加速 |
|------|--------|--------|------|
| 3000 msgs 每次请求裁剪+估算 | ~10.9 ms | ~1.2 ms | 9× |
| `estimate_tokens(orig)` | 全量 O(n) | 增量 O(1) | ∞ |
| `should_compact_local` | `estimate_messages_tokens` O(n) | `pruned_tokens()` 懒缓存 | ~100× |

## 0.1.0

初始版本。
