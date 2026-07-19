# 模块架构文档

> 模块拆分路线详见：[Module Split Plan](module_split_plan.md)。后续拆分 CMake target 时应保持该文档中的依赖方向和验证门禁。


## 模块结构

```
源码采用单树布局（头文件与 .cpp 同目录，无独立的 include/ 层）：

src/
├── agent/                     # Agent 编排层
│   ├── core/                  # （扁平，无 interface/ 子目录）
│   │   ├── agent_core.hpp       # 5 个服务接口 + Agent 主类 + 沙箱
│   │   ├── core_types.hpp       # 核心数据类型（SkillDefinition / HttpResponse / CommandResult 等）
│   │   ├── event_sink.hpp       # StreamEventSink / ToolEventSink / OrchestrationEventSink（ISP 三层接口）
│   │   ├── sub_agent_config.hpp # SubAgentConfig + SessionType 枚举（原 base/config/）
│   │   ├── agent_core.cpp, default_services.cpp
│   ├── sub_agent_types.hpp        # SubAgentTask / SubAgentResult / SubAgentStatus 完整定义
│   ├── execution/              # 执行原语层
│   │   ├── interceptor.hpp       # IInterceptor 接口（before_llm/before_tools/after_tools/should_stop + name()）
│   │   ├── loop.hpp/cpp          # ExecutionLoop — ReAct 核心循环（纯循环，模式逻辑由拦截器注入）
│   │   ├── service_interface.hpp # IExecutionLoopServices 接口（ExecutionLoop 与 ProviderClient 解耦）
│   │   ├── timeout_policy.hpp    # IToolTimeoutPolicy 接口 + DefaultTimeoutPolicy
│   │   ├── loop_snapshot.hpp     # LoopSnapshot（原名 InterceptorContext），含 step/total_calls/max_steps/max_calls/elapsed()
│   │   └── interceptors/         # 内置拦截器实现
│   │       ├── plan_interceptor.hpp/cpp        # PlanInterceptor：计划模式工具过滤 + 终态停止
│   │       └── compaction_interceptor.hpp/cpp  # CompactionInterceptor：上下文软压缩 + 溢出恢复
│   └── runtime/
│       ├── runtime.hpp / runtime.cpp              # Runtime（汇聚全部服务，构造函数 private）
│       ├── runtime_factory.hpp / runtime_factory.cpp  # RuntimeFactory（16 个 init 方法从 Runtime 移入）
│       ├── lifecycle_manager.hpp / lifecycle_manager.cpp  # LifecycleManager（生命周期状态机）
│       ├── runtime_run_session.cpp                # 会话执行主路径
│       ├── sub_agent_runtime.hpp / sub_agent_runtime.cpp  # SubAgentRuntime（独立类，非 Runtime 内部嵌套）
│       ├── tool_context.hpp         # IToolContext 接口 + ToolContext 实现
│       ├── memory_context.hpp       # IMemoryContext 接口 + MemoryContext 实现
│       ├── orchestration_context.hpp  # IOrchestrationContext 接口 + OrchestrationContext 实现
│       ├── service_bundles.hpp
│       └── application/            # 原 src/application/，已合并至此
│           ├── command_pipeline.hpp/cpp, command_governance.hpp/cpp
│           ├── runtime_execution.hpp/cpp, workspace_resolver.hpp/cpp
│           ├── command.hpp, request_context.hpp, command_descriptor_factory.hpp/cpp
│
├── acp/                       # Agent Communication Protocol（一级模块，非 capabilities 子目录）
│   ├── acp.hpp                # ACP 公共入口
│   ├── core/                  # 核心类型：ACPMessage / ContentBlock / 枚举 / ProtocolVersion
│   ├── types/                 # 协议类型：ToolCallRequest / ToolCallResult（从 capabilities/tool 迁入）
│   │   └── tool_call_types.hpp
│   ├── codec/                 # JSON 编解码 / 协议无关序列化器
│   ├── stream/                # StreamHandlers + 流式事件分发
│   └── adapter/               # 工具协议适配
│
├── cli/                       # 命令行界面
│   ├── args.hpp               # 声明式 CLI 解析器（子命令 + 链式 API + 自动帮助）
│   ├── render/                # 终端渲染子系统
│   │   ├── renderer.hpp       # Renderer 纯虚拟接口 + 工厂函数
│   │   ├── theme.hpp          # Dracula 风格主题（暗色+亮色）
│   │   ├── terminal.hpp       # 终端能力检测 + ANSI 转义码生成
│   │   ├── markdown.hpp       # Markdown 流式渲染器（ANSI 重绘方案）
│   │   ├── highlight.hpp      # 语法高亮器（10+ 语言预编译正则）
│   │   ├── spinner.hpp        # 异步等待动画
│   │   ├── display_config.hpp # 显示配置（可从 JSON 加载）
│   │   ├── cli_app.hpp        # CliApp 封装（Agent ↔ Renderer 桥接）
│   │   └── [renderer.cpp, cli_app.cpp, markdown.cpp, highlight.cpp]
│   ├── repl/                  # 交互式行编辑子系统
│   │   ├── terminal_io.hpp    # 终端 raw mode + 按键读取（跨平台）
│   │   ├── input_buffer.hpp   # 行内容 + 光标管理
│   │   ├── history_store.hpp  # 输入历史 + 持久化（~/.bengear/history）
│   │   ├── completer.hpp      # 补全器接口 + SlashCompleter
│   │   ├── line_editor.hpp    # 行编辑器（组合上述组件）
│   │   ├── chat_repl.hpp      # 聊天 REPL（Agent + LineEditor + CliApp）
│   │   └── [terminal_io.cpp, line_editor.cpp, chat_repl.cpp, history_store.cpp]
│   ├── app.cpp                # 应用入口
│   ├── app_commands.cpp       # 子命令注册
│   └── session_runner.cpp     # 会话执行器
│
├── base/                      # 高性能基础组件层
│   ├── config/                # 配置管理（settings.hpp, loader.hpp/cpp）
│   ├── net/                   # 网络层（http, connection_pool, event_loop, socket, tcp_stream）
│   ├── log/                   # 日志层（异步采集+格式化，stdout/file/tcp 输出）
│   ├── concurrency/           # 并发组件（thread_pool, lock_free, rate_limiter.hpp — TokenBucketRateLimiter + PerResourceRateLimiter）
│   ├── memory/                # 内存管理（pool.hpp/cpp）
│   ├── container/             # 容器（object_pool, format, string）
│   ├── io/                    # I/O（buffer, file, filesystem.hpp — IFileSystem 接口 + RealFileSystem）
│   ├── json/                  # JSON 解析器（DOM + SIMD 加速）
│   ├── platform/              # 平台抽象（OS, FileLock, subprocess, crypto, terminal）
│   ├── compress/              # 压缩抽象（CompressEngine, zlib 后端）
│   ├── utils/                 # 工具函数（json 工具, string_utils）
│   ├── core/                  # 运行时边界
│   └── tier_paths.hpp         # 三层级路径（global/user/workspace）
│
├── capabilities/              # Capability 抽象层
│   ├── capability.hpp         # ICapability + CapabilityBase CRTP 基类
│   ├── capability_registry.hpp # CapabilityRegistry 单例 + CapabilityRegistrar 静态注册
│   ├── tool/                  # 工具子系统
│   │   ├── types.hpp/cpp      # 工具类型定义
│   │   ├── registry.hpp/cpp   # 工具注册表（线程安全，shared_mutex）
│   │   ├── manager.hpp/cpp    # 工具调用管理器
│   │   ├── file_tools.cpp     # 文件工具（从 builtin_tools.cpp 拆分）
│   │   ├── shell_tools.cpp    # shell 工具
│   │   ├── http_tools.cpp     # HTTP 工具
│   │   ├── extended_tools.cpp # 扩展工具
│   │   ├── replace_tools.cpp  # 替换工具
│   │   ├── search_content_tools.cpp # 搜索内容工具
│   │   ├── env_tools.cpp      # 环境变量工具
│   │   ├── image_tools.cpp    # 图片工具
│   │   ├── builtin_tools.hpp/cpp  # 内置工具总入口（原 1240 行，已拆分为 8 个文件）
│   │   ├── sub_agent_tools.hpp/cpp # delegate_task / delegate_tasks 工具
│   │   ├── skill_tools.hpp    # 技能工具（get_skill + 管理工具）
│   │   ├── memory_tools.hpp   # 记忆工具引用注册
│   │   ├── workspace_tools.hpp # 工作空间工具引用注册
│   │   ├── workflow_tools.hpp # 工作流工具引用注册
│   │   └── history_tools.hpp  # 历史工具引用注册
│   ├── skill/                 # 技能核心类型与逻辑（skill.hpp/cpp, zip_extract）
│   ├── mcp/                   # MCP 协议客户端（mcp_client.hpp/cpp, mcp_config.hpp）
│   ├── git/                   # Git 能力服务
│   ├── test_loop/             # 测试循环能力服务
│   └── patch/                 # 补丁能力服务
│
├── domain/                    # 领域事件与错误类型
│   ├── event.hpp/cpp          # DomainEvent（纯净化：ToolCallPayload / ToolResultPayload / TokenUsage，无 tool/llm 类型依赖）
│   ├── errors.hpp             # 领域错误类型
│   ├── result.hpp             # AppResult 统一结果类型
│   └── result_adapters.hpp    # 结果适配器
│
├── llm/                       # LLM 协议层
│   ├── provider_interface.hpp # IProviderClient 虚基类
│   ├── provider_client.hpp/cpp # ProviderClient（故障转移 + 冷却追踪）
│   ├── openai_client.hpp/cpp  # OpenAI 客户端（实现 IProviderClient）
│   ├── anthropic_client.hpp/cpp # Anthropic 客户端（实现 IProviderClient）
│   ├── provider_registry.hpp  # Provider 注册表（静态 registrar）
│   ├── chat.hpp               # 聊天请求/响应
│   ├── conversation_history.hpp/cpp # 对话历史
│   ├── stream.hpp             # 流式响应（StreamHandlers + StreamToolCallDelta）
│   ├── retry.hpp              # 重试机制
│   ├── adapter.hpp            # 协议适配
│   ├── usage.hpp              # Token 用量统计
│   ├── cooldown_tracker.hpp   # 故障转移冷却追踪
│   └── internal/              # 内部解析器（anthropic_parser, openai_parser, sse）
│
├── memory/                    # 记忆系统
│   ├── store.hpp/cpp          # MemoryStore（跨进程文件锁 + 原子写入）
│   ├── episode.hpp/cpp        # EpisodeStore（每日情景）
│   ├── context.hpp/cpp        # ContextBuilder（PromptSection 位掩码 + PromptMode 枚举）
│   ├── compactor.hpp/cpp      # Compactor（软/硬阈值 + 持久化缓存）
│   ├── context_pruner.hpp/cpp # ContextPruner（L0–L4 渐进式裁剪）
│   ├── updater.hpp/cpp        # MemoryUpdater（LLM 驱动 + 重试 + 标签提取）
│   ├── section_merge.hpp/cpp  # merge_sections（last-wins）
│   ├── types.hpp              # 记忆类型定义
│   └── memory_tool_registration.cpp  # 记忆工具注册（原 tools/memory_tools 业务逻辑迁移至此）
│
├── orchestration/             # 编排层
│   ├── plan.hpp/cpp           # PlanManager（两态状态机 + PromptMode 驱动 plan 流程）
│   ├── todo.hpp/cpp           # TodoManager（任务跟踪）
│   ├── event.hpp              # ExecutionEvent（编排事件）
│   ├── serializer.hpp/cpp     # 计划序列化
│   ├── store.hpp/cpp          # 计划持久化存储
│   └── plan_parser.cpp        # 计划解析器
│
├── plugins/                   # 插件加载器层
│   ├── plugin_loader.hpp/cpp  # 动态库加载（dlopen/LoadLibrary）
│   └── plugin_abi.hpp         # 插件 ABI 定义
│
├── server/                    # HTTP/WebSocket 服务端
│   ├── api/                   # API 服务抽象
│   │   ├── *_types.hpp        # 虚基类（FileService, SessionService, …，非 std::function）
│   │   ├── *_api.hpp/cpp      # 路由注册
│   │   ├── handlers.hpp/cpp   # 请求处理器
│   │   └── result_presenter.hpp/cpp  # 结果展示器
│   ├── callback/              # 事件回调和序列化
│   │   └── workflow_event_projection.hpp/cpp  # 工作流事件投影
│   ├── composition/           # 服务组合层
│   │   ├── basic_api_composition.hpp/cpp  # API 组合
│   │   ├── command_api_composition.hpp/cpp # 命令 API 组合
│   │   ├── server_composition.hpp/cpp      # 服务器组合
│   │   └── application_services.hpp/cpp    # 应用服务
│   ├── core/                  # 服务器核心
│   │   ├── server.hpp/cpp     # Server 类
│   │   ├── server_lifecycle.cpp  # 服务器生命周期
│   │   └── router.hpp/cpp     # HTTP 路由
│   ├── http/                  # HTTP 协议
│   │   ├── parser.hpp/cpp     # HTTP 解析器
│   │   └── static_files.hpp/cpp  # 静态文件服务
│   ├── session/               # 会话管理
│   │   └── pool.hpp/cpp       # SessionPool
│   ├── ws/                    # WebSocket 协议
│   │   ├── handler.hpp/cpp    # WsHandler
│   │   ├── protocol.hpp/cpp   # WS 协议
│   │   ├── session_message_dispatcher.hpp/cpp  # 消息分发
│   │   └── ws_session_manager.hpp/cpp  # WsSessionManager（从 Server 提取）
│   └── auth/                  # 认证
│       └── auth.hpp/cpp       # Auth 中间件
│
├── workflow/                  # 工作流引擎
│   ├── workflow_engine.hpp/cpp   # WorkflowEngine（DAG 调度 + 命名空间隔离）
│   ├── workflow_templates.hpp    # 全局模板库
│   ├── workflow_resources.hpp    # 工作流共享资源
│   ├── dag.hpp                   # DAG 数据结构
│   ├── scheduler.hpp/cpp         # DAG 调度器
│   ├── executor.cpp              # 任务执行器
│   ├── task.hpp                  # ITask 接口
│   ├── task_types.hpp/cpp        # LLMTask / ToolTask / ConditionTask / SubflowTask
│   ├── types.hpp                 # 基础类型
│   ├── namespace.hpp             # 命名空间隔离（显式参数，非 thread_local）
│   ├── metrics.hpp/cpp           # 指标收集
│   ├── visualizer.hpp/cpp        # Mermaid/DOT 可视化（已合并入 bengear_workflow，不再独立 target）
│   ├── human_approval.hpp/cpp    # 人工审批
│   ├── approval_manager.cpp      # 审批管理器
│   ├── storage/                  # 工作流持久化
│   └── workflow_tool_registration.cpp  # 工作流工具注册（原 tools/workflow_tools 业务逻辑迁移至此）
│
├── workspace/                 # 工作空间管理
│   ├── manager.hpp/cpp        # WorkspaceManager（CRUD + 软删除/恢复）
│   ├── session.hpp/cpp        # Session（独占 history/Compactor/MemoryUpdater）
│   ├── history_db.hpp/cpp     # HistoryDB（FTS5 全文检索 + sessions 元数据表，原 1504 行已拆分为 4 文件）
│   ├── history_db_impl.hpp    # HistoryDB 内部实现头文件
│   ├── history_db_sessions.cpp  # 会话管理（从 history_db.cpp 拆分）
│   ├── history_db_search.cpp    # 全文检索（从 history_db.cpp 拆分）
│   ├── history_db_state.cpp     # 状态管理（从 history_db.cpp 拆分）
│   ├── history_exporter.hpp/cpp # HistoryExporter
│   ├── types.hpp              # 工作空间类型定义
│   └── history_tool_registration.cpp  # 历史工具注册（原 tools/history_tools 业务逻辑迁移至此）
│
├── ben_gear.hpp               # 主头文件
├── main.cpp                   # 程序入口
└── bengear_pch.hpp            # 预编译头
```


## 代码组织

### 单树布局

项目采用头文件与实现文件同目录的单树布局（`src/<module>/`），无独立的 `include/` 层：

- **头文件** — 类定义、函数声明、内联函数、模板实现
- **实现文件** — 成员函数实现、非内联函数、静态变量定义
- **header-only 例外** — 模板库（container/）、纯内联工具（utils/、log/）、部分 workflow 头文件保持 header-only

### 分离收益

- **编译加速**：修改实现只需重编单个 .cpp，无需重编所有依赖方
- **依赖隔离**：实现文件可引入额外头文件而不污染公共接口
- **增量构建**：CMake 按需编译变更的 .cpp，大幅缩短构建时间

### CMake 目标

| 目标 | 目录 | 说明 |
|------|------|------|
| bengear_memory_pool | base/memory/ | 内存池 |
| bengear_json | base/json/ | JSON 解析器 |
| bengear_base | base/ | 基础组件（config, log, platform, concurrency） |
| bengear_compress | base/compress/ | 压缩引擎 |
| bengear_core | base/core/ | 运行时边界 |
| bengear_net | base/net/ | 网络层（HTTP, TLS, 连接池, 事件循环） |
| bengear_domain | domain/ | 领域事件与错误类型 |
| bengear_acp | acp/ | ACP 协议 |
| bengear_llm | llm/ | LLM 客户端与流式处理 |
| bengear_tool_core | capabilities/tool/ | 工具核心（类型 + 注册表） |
| bengear_tool | capabilities/tool/ | 工具管理 + 内置工具 |
| bengear_skill | capabilities/skill/ | 技能系统 |
| bengear_mcp | capabilities/mcp/ | MCP 客户端 |
| bengear_plugins | plugins/ | 插件加载器 |
| bengear_memory | memory/ | 记忆系统 |
| bengear_workspace | workspace/ | 工作空间管理 |
| bengear_orchestration | orchestration/ | 编排层（计划 + 任务管理） |
| bengear_workflow | workflow/ | 工作流引擎（含可视化，原 visualizer 已合并） |
| bengear_agent_core | agent/core/ | Agent 核心（服务接口 + Agent + 沙箱） |
| bengear_agent_runtime | agent/runtime/ | Agent 运行时（含原 application/） |
| bengear_server_http | server/http/ | HTTP 解析 + 静态文件 + 路由 |
| bengear_server_ws | server/ws/ | WebSocket 协议 + 消息分发 |
| bengear_server_api | server/api/ | API 服务抽象（虚基类） |
| bengear_server_composition | server/composition/ + server/callback/ | 服务组合 + 事件回调 + WsSessionManager |
| bengear_server_core | server/core/ + server/session/ | 服务器核心 + 会话池 |
| bengear_cli_render | cli/render/ | 终端渲染 |
| bengear_cli_repl | cli/repl/ | REPL 交互 |
| bengear_cli_app | cli/ | CLI 应用入口 |

> 已删除的 target：`bengear_workflow_visualizer`（合并入 bengear_workflow）、`bengear_application`（合并入 bengear_agent_runtime/application/）。

## 模块职责

### 1. Agent 层
**职责**：Agent 编排和会话调度

**目录结构**：
- `agent/core/` — 扁平结构（无 interface/ 子目录）
  - `agent_core.hpp` — 5 个核心服务接口（IFileService / IWebAccessService / ISkillService / ICommandExecutor / IMCPService）+ Agent 主类 + SandboxedFileService / SandboxedCommandExecutor
  - `event_sink.hpp` — ISP 三层事件接口：StreamEventSink（LLM 流式）/ ToolEventSink（工具调用）/ OrchestrationEventSink（编排/计划）+ AgentEventSinks 聚合结构体 + Null 实现
  - `sub_agent_config.hpp` — SubAgentConfig + SessionType 枚举（原位于 base/config/）
- `agent/runtime/` — 运行时
  - `Runtime` — 汇聚全部服务，const 访问器线程安全
  - `SubAgentRuntime` — 独立子 Agent 运行时（非 Runtime 内部嵌套）
  - `IToolContext` / `ToolContext` — 工具子系统抽象接口 / 实现
  - `IMemoryContext` / `MemoryContext` — 记忆子系统抽象接口 / 实现
  - `IOrchestrationContext` / `OrchestrationContext` — 编排子系统抽象接口 / 实现
  - `application/` — 原 `src/application/`，命令管道、治理、执行、工作空间解析

**核心功能**：
- Session-based 对话管理（Agent 无状态，Session 独占 history）
- 流式/非流式双路径
- 流式增量工具调用解析
- 工具调用循环（max_tool_steps 轮次限制）
- ISP 事件通知（StreamEventSink / ToolEventSink / OrchestrationEventSink）
- 记忆压缩（Compactor）
- MCP 工具自动注册

**线程安全**：
- Agent 不持有可变状态
- Runtime 所有 const 访问器线程安全
- Session 独占资源无需加锁


#### agent/execution/ — 执行原语层
**职责**：通用 ReAct 执行循环 + 可组合拦截器链

**位置**：`agent/execution/`

**核心类**：
- `ExecutionLoop` — 通用执行原语，所有模式（normal / plan / sub_agent）共用。支持流式和非流式双路径。接受 `LoopConfig` 控制最大步数、最大工具调用数、最大并行工具数。
- `IInterceptor` — 拦截器接口，在 ReAct 循环的关键节点注入行为：
  - `before_llm()` — LLM 调用前（可修改 history、system prompt）
  - `before_tools()` — 工具执行前（可过滤/修改 tool_calls）
  - `after_tools()` — 工具执行后、写入历史前
  - `should_stop()` — 每轮末尾检查是否强制停止
- `LoopConfig` — 循环配置：`max_steps`（默认 20）、`max_calls`（默认 50）、`max_parallel_tools`（默认 5）

**设计要点**：
- `ExecutionLoop` 不持有 ProviderClient / ToolRegistry 所有权，仅持有引用
- 行为差异通过注入不同的 `IInterceptor` 组合实现（Plan 模式工具过滤、上下文压缩、步数限制等）
- 拦截器按添加顺序依次调用

### 2. CLI 层
**职责**：声明式命令行解析

**核心类**：`cli::Parser`

**关键功能**：
- 短标志：`-f`
- 长标志：`--flag`
- 短选项：`-o val`, `-oval`
- 长选项：`--opt val`, `--opt=val`
- 子命令：`workspace list`, `session delete`
- `--` 分隔符
- 自动生成帮助
- 链式 API（`.flag().option().command().on_default()`）

### 3. LLM 层
**职责**：LLM 协议实现

**核心类**：
- `IProviderClient` — 虚基类，定义 chat_async / chat_with_tools_async / chat_stream_async / chat_stream_with_tools_async
- `ProviderClient` — 统一客户端接口（故障转移 + 冷却追踪 + 协议分发）
- `OpenAiClient` / `AnthropicClient` — 实现 IProviderClient
- `ProviderRegistry` — 单例 + 静态 registrar，消除硬编码 if/else

**核心功能**：
- 原生工具调用 API（OpenAI + Anthropic）
- 流式响应解析（含增量工具调用 StreamToolCallDelta）
- 统一异步重试（with_retry_async / with_http_retry_async）
- 故障转移（冷却追踪 + 指数退避 + 30s 探针）
- TTFB 捕获 + 用量统计 + 上下文溢出自动恢复（L0→L4 渐进式裁剪+压缩）

**扩展指南**：
1. 实现 `ProviderClient::ClientFns` 签名的工厂函数
2. 在对应 `.cpp` 末尾写 `BEN_GEAR_REGISTER_PROVIDER(Provider::your_name, make_your_fns);`
3. 无需修改 `ProviderClient` 分发逻辑

### 4. 工具层
**职责**：工具注册、管理和执行

**位置**：`capabilities/tool/`

**核心类**：
- `ToolRegistry` — 线程安全注册表（shared_mutex）
- `ToolCallManager` — 调用管理器

**工具分布**：
- 内置工具（builtin_tools.cpp）：文件 10 个 / shell 1 个 / HTTP 2 个 / 搜索 2 个
- 技能工具（skill_tools.hpp）：get_skill + 管理工具
- 领域工具注册已迁移至对应模块：
  - `memory/memory_tool_registration.cpp` — 记忆工具
  - `workflow/workflow_tool_registration.cpp` — 工作流工具
  - `workspace/history_tool_registration.cpp` — 历史工具
  - `capabilities/skill/skill_tool_registration.cpp` — 技能工具

### 5. 技能层
**职责**：技能发现、加载和渐进式披露

**位置**：`capabilities/skill/`

**核心类**：
- `SkillDefinition` — 技能定义
- `SkillLoader` — 技能加载器

**3 级加载**：
- Level 1：系统提示注入元数据
- Level 2：`get_skill` 按需加载完整内容
- Level 3：`read_file`/`execute_command` 访问资源

### 6. MCP 层
**职责**：MCP 协议客户端

**位置**：`capabilities/mcp/`

**核心类**：
- `MCPClient` — 单服务器连接（stdio + HTTP）
- `MCPManager` — 多服务器管理 + ThreadPool 并行执行

### 7. 记忆系统
**职责**：三层级记忆存储、上下文压缩和记忆更新

**位置**：`memory/`

**核心类**：
- `MemoryStore` — 三层级存储（MEMORY.md / SOUL.md / RULES.md）
- `EpisodeStore` — 每日情景（YYYYMMDD.md）
- `ContextBuilder` — PromptSection 位掩码控制区段选择 + PromptMode 枚举控制模式指令（plan_reviewing / plan_executing / sub_agent）；区段按 identity → directives → skills → rules → soul → user → memory → workspace → mode 固定顺序组装；build() 不再接受 exclude_character 参数
- `Compactor` — 软/硬双阈值压缩 + 持久化缓存
- `ContextPruner` — L0–L4 渐进式上下文裁剪
- `MemoryUpdater` — LLM 驱动更新 + 重试 + 标签提取
- `merge_sections()` — last-wins section 合并

### 8. 工作空间
**职责**：多用户多工作空间管理

**位置**：`workspace/`

**核心类**：
- `WorkspaceManager` — CRUD + 软删除/恢复 + 默认模板
- `Session` — 独占 history/Compactor/MemoryUpdater
- `TierPaths` — 三层级路径（global/user/workspace）

### 9. 编排层
**职责**：计划管理、任务跟踪、事件定义

**位置**：`orchestration/`

**核心类**：
- `PlanManager` — 两态状态机（normal/planning），read-only 约束。Plan 模式通过 prompt 约束实现（非工具过滤）：ContextBuilder 根据模式注入对应 PromptMode（plan_reviewing / plan_executing）；CLI 提供 /plan、/approve、/cancel 完整流程
- `TodoManager` — 任务跟踪与状态管理
- `ExecutionEvent` — 编排事件定义

### 10. 领域事件层
**职责**：core 到 UI/API/日志的唯一结构化事件边界

**位置**：`domain/`

**核心类型**：
- `DomainEvent` — 纯净化事件（不包含 ANSI/Markdown/HTTP/WebSocket/CLI 展示细节）
- `ToolCallPayload` / `ToolResultPayload` — 序列化工具调用的 tagged wrapper
- `domain::TokenUsage` — 领域层自己的用量表示（解耦自 llm::TokenUsage）
- `domain::EventSink` — 事件接收接口

### 11. 工作流引擎
**职责**：DAG 工作流调度与执行

**位置**：`workflow/`

**核心类**：
- `WorkflowEngine` — DAG 调度 + 命名空间隔离（命名空间显式参数，非 thread_local）
- `LLMTask` — 通过 WorkflowResources 执行，`execute_async()` 协程路径
- `ToolTask` / `ConditionTask` / `SubflowTask` — 各类任务实现
- `Scheduler` — DAG 调度器

### 12. Capability 抽象层
**职责**：统一所有能力服务的基类接口与注册表

**位置**：`capabilities/`

**核心类**：
- `ICapability` — 纯虚基类，要求实现 `name()` 与 `init()`
- `CapabilityBase<Derived>` — CRTP 基类
- `CapabilityRegistry` — 单例注册表，延迟初始化
- `CapabilityRegistrar` — 静态注册辅助

### 13. 插件加载器层
**职责**：运行时动态加载插件

**位置**：`plugins/`

**核心类**：
- `PluginLoader` — `dlopen`/`LoadLibrary` 加载 + 自动注册

### 14. ACP 层
**职责**：Agent Communication Protocol 统一协议层

**位置**：`acp/`（一级模块，非 capabilities 子目录）

**核心类**：
- `ACPMessage` — 统一消息
- `ContentBlock` — 内容块（text/tool_use/tool_result）
- `ProtocolVersion` — 协议版本（定义于 `acp/core/types.hpp`）
- `ToolCallRequest` / `ToolCallResult` — 工具调用协议类型（定义于 `acp/types/tool_call_types.hpp`，从 `capabilities/tool` 迁入）
- JSON 编解码 / 流式事件分发 / 工具协议适配

### 15. 服务端
**职责**：HTTP/WebSocket 服务器

**位置**：`server/`

**核心类**：
- `Server` — HTTP 服务器（core/）
- `WsSessionManager` — WS 会话管理器（从 Server 提取，独立类）
- `FileService` / `SessionService` 等 — API 服务虚基类（非 std::function）
- `SessionPool` — 会话池

### 16. 网络层
**职责**：网络通信

**核心功能**：
- 原生 HTTP/HTTPS（TlsEngine 抽象，MbedTLS/OpenSSL/Schannel 多后端）
- 连接池（shared_mutex + ObjectPool）
- 协程异步（EventLoop + TcpStream + net::Task<T>）
- WakeupFd 跨平台唤醒

### 17. 日志层
**职责**：异步日志

**核心功能**：
- 前端轻量采集 + 后端异步格式化
- Stdout / File（日期+PID 隔离 + 自动轮转）/ TCP Server

## 依赖关系

```
┌─────────────────────────────────────────────────────────┐
│                    bengear_cli_app                       │
└───────────────────────────┬─────────────────────────────┘
                            │
        ┌───────────────────┴───────────────────┐
        │                                       │
┌───────▼──────────┐                  ┌─────────▼────────┐
│ bengear_cli_repl │                  │ bengear_server   │
└───────┬──────────┘                  │   _composition   │
        │                             └─────────┬────────┘
┌───────▼──────────┐                            │
│ bengear_cli_     │    ┌───────────────────────┼───────────────┐
│     render       │    │                       │               │
└───────┬──────────┘    │               ┌───────▼───────┐ ┌─────▼──────┐
        │               │               │ bengear_server│ │ bengear_   │
        │               │               │     _api      │ │ server_ws  │
        │               │               └───────┬───────┘ └─────┬──────┘
        │               │                       │               │
┌───────▼───────────────▼───────────────────────▼───────────────▼──────┐
│                     bengear_agent_runtime                             │
│  (Runtime, SubAgentRuntime, Tool/Memory/Orchestration contexts,       │
│   application/)                                                       │
└───────┬───────┬───────┬───────┬───────┬───────┬──────────────────────┘
        │       │       │       │       │       │
┌───────▼─┐ ┌───▼───┐ ┌─▼─────┐ ┌─▼───┐ ┌─▼───┐ ┌─▼──────────┐
│bengear_ │ │bengear│ │bengear│ │benge│ │benge│ │bengear_     │
│agent_   │ │_llm   │ │_memory│ │ar_  │ │ar_  │ │orchestration│
│core     │ │       │ │       │ │work │ │works│ │             │
└────┬────┘ └───┬───┘ └───┬───┘ │flow │ │pace │ └──────┬──────┘
     │          │         │     └──┬──┘ └──┬──┘        │
     │    ┌─────┘    ┌────┘        │       │           │
     │    │          │             │       │     ┌─────▼─────┐
     │ ┌──▼────┐ ┌───▼────┐ ┌─────▼──┐ ┌──▼───┐ │bengear_   │
     │ │bengear│ │bengear │ │bengear │ │benge │ │plugins    │
     │ │_mcp   │ │_skill  │ │_tool   │ │ar_   │ └───────────┘
     │ └───┬───┘ └───┬────┘ └───┬────┘ │domain│
     │     │         │          │      └──┬───┘
     │     │    ┌────┘    ┌─────┘         │
     │     │    │         │               │
     │ ┌───▼────▼─────────▼───────────────▼──┐
     │ │          bengear_net                │
     │ └────────────────┬────────────────────┘
     │                  │
 ┌───▼──────────────────▼──────────────────────┐
 │              bengear_base                    │
 │  (config, log, platform, concurrency,        │
 │   bengear_json, bengear_memory_pool)         │
 └──────────────────────────────────────────────┘
     │                  │
 ┌───▼──────┐    ┌──────▼──────┐
 │bengear_  │    │bengear_     │
 │compress  │    │acp          │
 └──────────┘    └─────────────┘
```

## 设计原则

### 高内聚
- 每个模块职责单一
- 相关功能聚合在一起
- 模块内部高度相关

### 低耦合
- 模块间通过接口交互
- Runtime 依赖注入（IToolContext / IMemoryContext / IOrchestrationContext）
- 易于单元测试和替换

### 可扩展
- 易于添加新功能
- IProviderClient 虚基类支持新 LLM 提供商
- 插件化架构

### 易维护
- 结构清晰
- 命名规范
- 文档完善

## 模块接口规范

### 命名空间
```cpp
namespace ben_gear {
    namespace agent::core { /* Agent 核心服务接口 + Agent */ }
    namespace agent::runtime { /* Runtime + contexts + application */ }
    namespace acp { /* ACP 协议层 */ }
    namespace capabilities::tool { /* 工具层 */ }
    namespace capabilities::skill { /* 技能层 */ }
    namespace capabilities::mcp { /* MCP 层 */ }
    namespace orchestration { /* 编排层 */ }
    namespace domain { /* 领域事件/错误 */ }
    namespace llm { /* LLM 层 */ }
    namespace memory { /* Memory 层 */ }
    namespace workspace { /* Workspace 层 */ }
    namespace workflow { /* Workflow 层 */ }
    namespace plugins { /* Plugin Loader 层 */ }
    namespace cli { /* CLI 层 */ }
    namespace cli::render { /* 渲染层 */ }
    namespace cli::repl { /* REPL 层 */ }
    namespace server { /* 服务端 */ }
    namespace net { /* Net 层 */ }
    namespace log { /* Log 层 */ }
    namespace base::config { /* Config 层 */ }
}
```

### 头文件组织
```cpp
// Agent 层
#include "agent/core/agent_core.hpp"            // 服务接口 + Agent + 沙箱
#include "agent/core/event_sink.hpp"             // ISP 事件接口
#include "agent/core/sub_agent_config.hpp"       // SubAgentConfig + SessionType
#include "agent/runtime/runtime.hpp"             // Runtime
#include "agent/runtime/sub_agent_runtime.hpp"   // SubAgentRuntime
#include "agent/runtime/tool_context.hpp"        // IToolContext / ToolContext
#include "agent/runtime/memory_context.hpp"      // IMemoryContext / MemoryContext
#include "agent/runtime/orchestration_context.hpp" // IOrchestrationContext / OrchestrationContext

// CLI 层
#include "cli/args.hpp"                          // CLI 解析器
#include "cli/render/cli_app.hpp"                // 渲染 + Agent 桥接
#include "cli/repl/chat_repl.hpp"                // 交互式 REPL

// 配置
#include "base/config/settings.hpp"              // 配置定义
#include "base/config/loader.hpp"                // 配置加载

// LLM 层
#include "llm/provider_interface.hpp"            // IProviderClient 虚基类
#include "llm/provider_client.hpp"               // ProviderClient
#include "llm/stream.hpp"                        // 流式处理
#include "llm/retry.hpp"                         // 重试

// 工具层
#include "capabilities/tool/registry.hpp"        // ToolRegistry
#include "capabilities/tool/types.hpp"           // 工具类型

// 技能层
#include "capabilities/skill/skill.hpp"          // SkillDefinition / SkillLoader

// MCP 层
#include "capabilities/mcp/mcp_client.hpp"       // MCP 客户端

// 记忆层
#include "memory/store.hpp"                      // MemoryStore
#include "memory/compactor.hpp"                  // Compactor
#include "memory/context.hpp"                    // ContextBuilder

// 工作空间层
#include "workspace/manager.hpp"                 // WorkspaceManager
#include "workspace/session.hpp"                 // Session

// 编排层
#include "orchestration/plan.hpp"                // PlanManager

// 领域层
#include "domain/event.hpp"                      // DomainEvent
#include "domain/errors.hpp"                     // 领域错误

// 工作流层
#include "workflow/workflow_engine.hpp"          // WorkflowEngine

// 基础层
#include "base/net/http.hpp"                     // HTTP 客户端
#include "base/log/logger.hpp"                   // 日志
#include "base/platform/file_lock.hpp"           // 文件锁

// Capability 抽象
#include "capabilities/capability.hpp"           // ICapability
#include "capabilities/capability_registry.hpp"  // CapabilityRegistry

// 插件
#include "plugins/plugin_loader.hpp"             // PluginLoader
```

### 依赖规则
1. **单向依赖**：上层依赖下层，下层不依赖上层
2. **接口隔离**：通过虚基类交互（IToolContext / IMemoryContext / IOrchestrationContext, IProviderClient）
3. **最小依赖**：只依赖必要的模块
4. **Runtime 模式**：通过 shared_ptr 共享只读资源，避免重复构造

## 扩展指南

### 添加新 LLM 提供商
1. 实现 `IProviderClient` 虚基类
2. 实现 `ProviderClient::ClientFns` 签名的工厂函数
3. 在对应 `.cpp` 末尾写 `BEN_GEAR_REGISTER_PROVIDER(Provider::your_name, make_your_fns);`
4. 无需修改 `ProviderClient` 分发逻辑

### 添加新 Capability
1. 继承 `CapabilityBase<YourCapability>`，定义 `static constexpr const char* kName = "your_name";`
2. 在 `your_capability.cpp` 末尾写 `BEN_GEAR_REGISTER_CAPABILITY("your_name", YourCapability);`
3. 通过 `CapabilityRegistry::instance().get_or_create<YourCapability>("your_name", ws_ctx)` 获取实例

### 注册 Plugin
1. 编译动态库，导出 `extern "C" void ben_gear_plugin_init()`
2. 在 `ben_gear_plugin_init` 内调用 `BEN_GEAR_REGISTER_CAPABILITY` 或 `BEN_GEAR_REGISTER_PROVIDER`
3. 将 `.dll`/`.so` 放入配置的 `plugins_dir`
4. 启动时 `PluginLoader(plugins_dir).load_all()` 自动加载

### 添加新工具
1. 在 `capabilities/tool/` 目录添加工具实现
2. 使用 `tool::registry.register_tool()` 注册
3. 定义 JSON Schema 参数
4. 在 `register_all_tools` 中调用注册

### 添加新技能
1. 创建技能目录 `~/.bengear/skills/<name>/`
2. 编写 SKILL.md（frontmatter key: value + Markdown 指令）
3. 运行 `--list-skills` 验证发现

### 添加新 MCP 服务器
1. 在 config.json 的 `mcp_servers` 添加服务器配置
2. 运行 Agent 自动连接并注册工具

## 最佳实践

1. **模块边界清晰**：不在模块间共享内部实现
2. **接口注入**：通过虚基类（IToolContext / IMemoryContext / IOrchestrationContext, IProviderClient）解耦
3. **Session 隔离**：每个 Session 独占可变状态，无需加锁
4. **接口稳定**：公共接口保持向后兼容
5. **文档完善**：每个公共接口都有文档
6. **测试覆盖**：每个模块都有单元测试
7. **性能优化**：关键路径有性能测试
8. **日志规范**：异常路径 log::error_fmt，正常关键节点 log::info_fmt
