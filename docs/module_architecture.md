# 模块架构文档

## 模块结构

```
源码目录分为 include/（头文件）和 src/（实现文件）两层，
头文件声明接口，实现文件包含业务逻辑，加速编译、降低耦合。

include/ben_gear/ + src/ 对应关系：

ben_gear/
├── agent/                     # Agent 编排层
│   ├── agent.hpp              # Agent 主类（Session-based API，无状态调度器）
│   ├── agent_impl.hpp         # Agent 实现（流式步骤、工具循环）
│   ├── callbacks.hpp          # 回调接口（on_token/on_thinking/on_tool_call/on_tool_result）
│   ├── sub_agent_config.hpp   # 子 Agent 配置 + SessionType 枚举（轻量，无循环依赖）
│   ├── sub_agent.hpp          # 子 Agent 运行时（SubAgentRuntime/Event/Result/Task）
│   ├── shared_resources.hpp   # 共享资源（一次构建，多 Agent/多会话复用）
│   ├── plan_manager.hpp       # 计划管理器（两态状态机：normal/planning，read-only 约束）
│   └── [agent.cpp, shared_resources.cpp, sub_agent.cpp]
│
├── acp/                       # Agent Communication Protocol 统一协议层
│   ├── acp.hpp                # ACP 公共入口
│   ├── core/                  # 核心类型
│   │   ├── message.hpp        # 统一消息（ACPMessage）
│   │   ├── content_block.hpp  # 内容块（text/tool_use/tool_result）
│   │   └── types.hpp          # 枚举与基础类型
│   │   └── [message.cpp, content_block.cpp]
│   ├── codec/                 # 编解码
│   │   ├── json_codec.hpp     # ACP ↔ JSON 序列化
│   │   └── serializer.hpp     # 协议无关序列化器
│   ├── stream/                # 流式处理
│   │   ├── handler.hpp        # StreamHandlers + StreamToolCallDelta
│   │   └── dispatcher.hpp     # 流式事件分发
│   └── adapter/               # 提供商适配器
│       └── tool_adapter.hpp   # 工具协议适配
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
│   │   └── [renderer.cpp, cli_app.cpp]
│   └── repl/                  # 交互式行编辑子系统
│       ├── terminal_io.hpp    # 终端 raw mode + 按键读取（跨平台）
│       ├── input_buffer.hpp   # 行内容 + 光标管理（container::String）
│       ├── history_store.hpp  # 输入历史 + 持久化（~/.bengear/history）
│       ├── completer.hpp      # 补全器接口 + SlashCompleter（一级/二级）
│       ├── line_editor.hpp    # 行编辑器（组合上述组件）
│       ├── chat_repl.hpp      # 聊天 REPL（Agent + LineEditor + CliApp）
│       └── [terminal_io.cpp, line_editor.cpp, chat_repl.cpp, history_store.cpp]
│
├── config/                    # 配置管理层
│   ├── settings.hpp           # 配置定义（model_config 分组格式）
│   ├── loader.hpp             # 配置加载（7 层覆盖 + 环境变量）
│   └── [loader.cpp]
│
├── llm/                       # LLM 协议层
│   ├── anthropic_client.hpp   # Anthropic 客户端
│   ├── openai_client.hpp      # OpenAI 客户端
│   ├── provider_client.hpp    # 统一客户端接口（协议分发边界）
│   ├── adapter.hpp            # OpenAI/Anthropic 适配器（ACP ↔ 提供商格式）
│   ├── chat.hpp               # 聊天请求/响应
│   ├── http_helpers.hpp       # HTTP 辅助函数
│   ├── retry.hpp              # 重试机制（同步 + 异步 + HTTP 重试）
│   ├── stream.hpp             # 流式响应（StreamHandlers + StreamToolCallDelta）
│   ├── [adapter.cpp]
│   └── internal/              # 内部实现
│       ├── anthropic_parser.hpp  # Anthropic 流解析器
│       ├── openai_parser.hpp     # OpenAI 流解析器
│       └── sse.hpp               # SSE 解析
│
├── tool/                      # 工具层
│   ├── types.hpp              # 工具类型定义
│   ├── registry.hpp           # 工具注册表（线程安全，shared_mutex）
│   ├── manager.hpp            # 工具调用管理器
│   └── [types.cpp, registry.cpp, manager.cpp]
│
├── tools/                     # 工具注册与实现（header-only，内联注册）
│   ├── builtin_tools.hpp      # 内置工具（文件 10 个/shell 1 个/http 2 个/搜索 2 个）
│   ├── skill_tools.hpp        # 技能工具 + get_skill + 5 个管理工具
│   ├── memory_tools.hpp       # 记忆工具（7 个：读写记忆/灵魂/规范 + recall + episode）
│   ├── workspace_tools.hpp    # 工作空间工具（4 个：列表/创建/删除/恢复）
│   └── workflow_tools.hpp     # 工作流工具（8 个：创建/执行/状态/取消/列表/模板/可视化/导入导出）
│
├── skill/                     # 技能核心类型与逻辑
│   ├── skill.hpp              # 技能定义与加载器
│   ├── zip_extract.hpp        # 下载与解压辅助
│   └── [skill.cpp, zip_extract.cpp]
│
├── memory/                    # 记忆系统
│   ├── store.hpp              # 记忆存储（MemoryStore，跨进程文件锁 + 原子写入）
│   ├── episode.hpp            # 剧集存储（EpisodeStore，每日情景 + FileLock 安全追加）
│   ├── context.hpp            # 上下文构建器（ContextBuilder，7 步组装 + CJK token 估算）
│   ├── compactor.hpp          # 上下文压缩器（Compactor，软/硬阈值 + 持久化缓存）
│   ├── updater.hpp            # 记忆更新器（MemoryUpdater，LLM 驱动 + 重试 + 标签提取）
│   ├── section_merge.hpp      # 章节合并（merge_sections，last-wins）
│   ├── types.hpp              # 记忆类型定义
│   └── [store.cpp, episode.cpp, context.cpp, compactor.cpp, updater.cpp, section_merge.cpp]
│
├── workflow/                  # 工作流引擎
│   ├── workflow_engine.hpp    # 工作流引擎（DAG 调度 + 命名空间隔离）
│   ├── workflow_templates.hpp # 全局模板库
│   ├── workflow_resources.hpp # 工作流共享资源
│   ├── dag.hpp                # DAG 数据结构
│   ├── scheduler.hpp          # DAG 调度器
│   ├── executor.hpp           # 任务执行器
│   ├── task.hpp               # 任务定义
│   ├── task_types.hpp         # 任务类型（llm/tool/function）
│   ├── types.hpp              # 基础类型
│   ├── namespace.hpp          # 命名空间隔离
│   ├── storage.hpp            # 工作流持久化
│   ├── metrics.hpp            # 指标收集
│   ├── visualizer.hpp         # Mermaid/DOT 可视化
│   ├── human_approval.hpp     # 人工审批
│   └── [workflow_engine.cpp, scheduler.cpp, executor.cpp, task_types.cpp]
│
├── workspace/                 # 工作空间管理
│   ├── manager.hpp            # 工作空间管理器（WorkspaceManager，CRUD + 软删除/恢复）
│   ├── session.hpp            # 会话管理（Session，独占 history/Compactor/MemoryUpdater）
│   ├── conversation_history.hpp # 对话历史（ConversationHistory）
│   ├── history_db.hpp         # HistoryDB（FTS5 全文检索 + sessions 元数据表）
│   ├── history_exporter.hpp   # HistoryExporter（会话导出模块）
│   ├── uuid.hpp               # UUID 生成
│   ├── types.hpp              # 工作空间类型定义
│   └── [manager.cpp, session.cpp, conversation_history.cpp, history_db.cpp, history_exporter.cpp, uuid.cpp]
│
├── mcp/                       # MCP 协议层
│   ├── mcp_client.hpp         # MCP 客户端 + 管理器（stdio + HTTP 双传输 + ThreadPool 并行）
│   ├── mcp_config.hpp         # MCP 配置解析
│   └── [mcp_client.cpp]
│
├── base/                      # 高性能基础组件层
│   ├── net/                   # 网络层
│   │   ├── http.hpp           # 统一的 HTTP 客户端（内置连接池 + ObjectPool）
│   │   ├── connection_pool.hpp # 连接池（预热 + shared_mutex 读写锁）
│   │   ├── event_loop.hpp     # 事件循环
│   │   ├── socket.hpp         # Socket 封装
│   │   ├── task.hpp           # 协程任务
│   │   ├── tcp_stream.hpp     # TCP 流
│   │   └── [connection_pool.cpp, event_loop.cpp, socket.cpp, tcp_stream.cpp, wakeup_fd.cpp]
│   │
│   ├── log/                   # 日志层
│   │   ├── logger.hpp         # 日志记录器（前端轻量采集 + 后端异步格式化）
│   │   ├── sink.hpp           # 输出目标（Stdout / File 轮转 / TCP Server）
│   │   ├── level.hpp          # 日志级别
│   │   └── configure.hpp      # 日志配置
│   │
│   ├── memory/                # 内存管理
│   │   ├── pool.hpp           # 内存池（PoolStats 原子字段 + STL 兼容分配器）
│   │   └── [pool.cpp]
│   │
│   ├── concurrency/           # 并发组件
│   │   ├── thread_pool.hpp    # 线程池（工作窃取 + 动态调整）
│   │   ├── lock_free.hpp      # 无锁数据结构（Queue/Stack/RingBuffer）
│   │   └── [thread_pool.cpp]
│   │
│   ├── container/             # 容器（部分 header-only，部分有 cpp）
│   │   ├── string.hpp         # 高性能字符串（SSO + hash 委托 string_view + find 用 std::search）
│   │   ├── vector.hpp         # 动态数组（支持自定义分配器）
│   │   ├── map.hpp            # 哈希映射（开放寻址法 + 罗宾汉哈希 + 异构查找）
│   │   ├── format.hpp         # 格式化工具
│   │   ├── object_pool.hpp    # 对象池（FixedSizePool + free list）
│   │   └── [string.cpp]
│   │
│   ├── io/                    # I/O 组件
│   │   ├── buffer.hpp         # 高性能缓冲区
│   │   └── file.hpp           # 文件操作
│   │
│   ├── json/                  # JSON 解析器
│   │   ├── json.hpp           # JSON 公共接口（API 兼容 nlohmann/json）
│   │   ├── json_dom.hpp       # DOM 节点（JsonNode + 引用语义）
│   │   ├── json_parser.hpp    # 递归下降解析器
│   │   ├── json_serializer.hpp # 两遍序列化器
│   │   ├── json_simd.hpp      # SIMD 加速（字符串/结构探测）
│   │   └── [json.cpp, json_dom.cpp, json_parser.cpp, json_serializer.cpp, json_simd.cpp]
│   │
│   ├── platform/              # 平台抽象
│   │   ├── platform.hpp       # 平台接口（CPU、线程、进程、OS）
│   │   ├── os.hpp             # 操作系统接口 + compat 兼容层 + subprocess 安全子进程 + FileLock
│   │   ├── file_lock.hpp      # 跨平台文件锁（POSIX fcntl + Windows LockFileEx）
│   │   └── [platform.cpp]
│   │
│   └── utils/                 # 工具函数
│       ├── json.hpp           # JSON 工具
│       └── string_utils.hpp   # 字符串工具（to_lower/trim 等）
│
└── ben_gear.hpp               # 主头文件
```


## 头文件与实现分离

项目采用 **hpp/cpp 分离**的代码组织方式：

- `include/ben_gear/` — 头文件，仅包含声明和内联函数
- `src/` — 实现文件，包含业务逻辑

### 分离原则

1. **头文件只放声明**：类定义、函数声明、内联函数、模板实现
2. **实现文件放逻辑**：成员函数实现、非内联函数、静态变量定义
3. **header-only 例外**：模板库（container/）、配置结构（settings.hpp）、纯内联工具（utils/）保持 header-only

### 分离收益

- **编译加速**：修改实现只需重编单个 .cpp，无需重编所有依赖方
- **依赖隔离**：实现文件可引入额外头文件而不污染公共接口
- **增量构建**：CMake 按需编译变更的 .cpp，大幅缩短构建时间

### 已分离模块

| 模块 | 头文件 | 实现文件 |
|------|--------|---------|
| agent | agent.hpp, shared_resources.hpp | agent.cpp, shared_resources.cpp |
| acp/core | message.hpp, content_block.hpp | message.cpp, content_block.cpp |
| config | loader.hpp | loader.cpp |
| llm | adapter.hpp | adapter.cpp |
| mcp | mcp_client.hpp | mcp_client.cpp |
| memory | store/episode/context/compactor/updater/section_merge.hpp | 对应 6 个 .cpp |
| skill | skill.hpp, zip_extract.hpp | skill.cpp, zip_extract.cpp |
| tool | types/registry/manager.hpp | types.cpp, registry.cpp, manager.cpp |
| workspace | manager/session/conversation_history/history_db/history_exporter/uuid.hpp | 对应 6 个 .cpp |
| cli/render | renderer.hpp, cli_app.hpp | renderer.cpp, cli_app.cpp |
| cli/repl | chat_repl/line_editor/terminal_io/history_store.hpp | 对应 4 个 .cpp |
| base/net | connection_pool/event_loop/socket/tcp_stream.hpp | 对应 5 个 .cpp |
| base/json | json/json_dom/json_parser/json_serializer/json_simd.hpp | 对应 5 个 .cpp |
| base/memory | pool.hpp | pool.cpp |
| base/concurrency | thread_pool.hpp | thread_pool.cpp |
| base/container | string.hpp | string.cpp |
| base/platform | platform.hpp | platform.cpp |

### 保持 header-only 的模块

- **container**（map.hpp, vector.hpp, format.hpp, object_pool.hpp）— 模板库
- **tools/**（builtin_tools 等）— 内联工具注册
- **log/** — 前端轻量采集，内联优化
- **llm/ 内部**（parser、sse）— 模板化解析器
- **workflow 部分头文件**（dag.hpp, types.hpp, namespace.hpp 等）— 内联算法

## 模块职责

### 1. Agent 层
**职责**：Agent 编排和会话调度

**核心功能**：
- Session-based 对话管理（Agent 无状态，Session 独占 history）
- 流式/非流式双路径
- 流式增量工具调用解析
- 工具调用循环（max_tool_steps 轮次限制，max_tool_calls 累计限制，max_tool_calls_per_step 单轮限制）
- 回调通知机制
- 记忆压缩（Compactor）
- MCP 工具自动注册

**线程安全**：
- Agent 不持有可变状态
- SharedResources 所有 const 访问器线程安全
- Session 独占资源无需加锁

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

**核心功能**：
- 原生工具调用 API（OpenAI + Anthropic）
- 流式响应解析（含增量工具调用 StreamToolCallDelta）
- 协议适配（ProviderClient 统一分发）
- 统一异步重试（with_retry_async / with_http_retry_async）

### 4. 工具层
**职责**：工具注册、管理和执行

**核心类**：
- `ToolRegistry` — 线程安全注册表（shared_mutex）
- `ToolCallManager` — 调用管理器

**工具总数**：
- 内置工具：13+ 个（文件 10 + shell 1 + HTTP 2 + 搜索 2）
- 技能工具：6 个（get_skill + 5 管理工具）
- 记忆工具：7-8 个
- 工作空间工具：4 个
- MCP 工具：动态发现

### 5. 技能层
**职责**：技能发现、加载和渐进式披露

**核心类**：
- `SkillDefinition` — 技能定义
- `SkillLoader` — 技能加载器

**3 级加载**：
- Level 1：系统提示注入元数据
- Level 2：`get_skill` 按需加载完整内容
- Level 3：`read_file`/`execute_command` 访问资源

### 6. MCP 层
**职责**：MCP 协议客户端

**核心类**：
- `MCPClient` — 单服务器连接（stdio + HTTP）
- `MCPManager` — 多服务器管理 + ThreadPool 并行执行

### 7. 记忆系统
**职责**：三层级记忆存储、上下文压缩和记忆更新

**核心类**：
- `MemoryStore` — 三层级存储（MEMORY.md / SOUL.md / RULES.md）
- `EpisodeStore` — 每日情景（YYYYMMDD.md）
- `ContextBuilder` — 7 步系统提示组装 + CJK token 估算
- `Compactor` — 软/硬双阈值压缩 + 持久化缓存
- `MemoryUpdater` — LLM 驱动更新 + 重试 + 标签提取
- `merge_sections()` — last-wins section 合并

### 8. 工作空间
**职责**：多用户多工作空间管理

**核心类**：
- `WorkspaceManager` — CRUD + 软删除/恢复 + 默认模板
- `Session` — 独占 history/Compactor/MemoryUpdater
- `TierPaths` — 三层级路径（global/user/workspace）

**职责**：基于白名单的工具过滤

**核心类**：

### 10. 网络层
**职责**：网络通信

**核心功能**：
- 原生 HTTP/HTTPS（TlsEngine 抽象，MbedTLS/OpenSSL/Schannel 多后端）
- 连接池（shared_mutex + ObjectPool）
- 协程异步（EventLoop + TcpStream + sync_wait）
- 多 IoContext 架构（io / workflow / util 三上下文）
- 事件驱动 sync_wait（FinalAwaiter on_complete，零轮询）
- WakeupFd 跨平台唤醒（Linux: eventfd, macOS: pipe, Windows: WSAEventSelect）
- 连接池复用（TLS/非 TLS，keep-alive，空闲超时淘汰）
- URL 解析器 + HTTP/1.1 请求构建
- Chunked transfer 解码
- 预热支持

### 12. TLS 抽象层
**职责**：后端无关的 TLS 操作

**核心功能**：
- TlsEngine 抽象接口（握手、加密读写、优雅关闭）
- TlsEngine::Session 会话管理
- TlsConfig 配置（证书验证、SNI、协议版本）
- 多后端支持（MbedTLS/OpenSSL/Schannel/none）
- 全局实例管理（global_tls_engine / set_global_tls_engine）
- 连接池类型安全（unique_ptr<Session> 替代 void*）

### 13. 压缩抽象层
**职责**：后端无关的压缩/解压操作

**核心功能**：
- CompressEngine 抽象接口（inflate/deflate）
- zlib 后端（vendor，third_party/zlib/）
- 全局实例管理（global_compress_engine / set_global_compress_engine）

### 14. 测试框架层
**职责**：自研轻量测试框架

**核心功能**：
- gtest 宏兼容（TEST/EXPECT_*/ASSERT_*/TEST_F）
- --filter / --verbose 命令行选项
- 函数式临时目录工具（make_tmp_dir / remove_tmp_dir）
- EXPECT_THAT + HasSubstr gmock 兼容

### 11. 日志层
**职责**：异步日志

**核心功能**：
- 前端轻量采集 + 后端异步格式化
- Stdout / File（日期+PID 隔离 + 自动轮转）/ TCP Server
- 日志格式化接口（log::info_fmt / log::error_fmt / log::debug_fmt）

## 依赖关系

```
┌─────────────────────────────────────────┐
│              Application                 │
└─────────────────────────────────────────┘
                     │
     ┌───────────────┴───────────────┐
     │                               │
 ┌───▼────┐                    ┌───▼────┐
 │ Agent  │                    │ Config │
 └───┬────┘                    └───┬────┘
     │                               │
 ┌───┴───────────────────────────────┴───┐
     │           │         │         │
┌────▼───┐ ┌────▼───┐ ┌───▼────┐ ┌──▼──────┐
│  LLM   │ │ Skill  │ │  Tool  │ │ Memory  │
└────┬───┘ └────┬───┘ └───┬────┘ └────┬────┘
     │          │         │            │
     │    ┌─────┘   ┌─────┘     ┌─────┘
     │    │         │           │
     │ ┌──▼───┐ ┌──▼─────┐ ┌──▼──────┐
     │ │ MCP  │ │ Tools  │ │Workspace│
     │ └──┬───┘ └────────┘ └────┬────┘
     │    │                      │
     │    │  ┌──────────┐ ┌─────▼─────┐
     │    │  │   Role   │ │  Session  │
     │    │  └────┬─────┘ └───────────┘
     │    │       │
     └────┼───────┼────────►  Net
          │       │
          │  ┌────▼─────┐
          │  │   Log    │
          │  └────┬─────┘
          │       │
          │  ┌────▼─────┐
          │  │Platform  │
          │  └────┬─────┘
          │       │
          │  ┌────▼─────┐
              │       │
              │  ┌────▼─────┐
              │  │  TLS     │
              │  └────┬─────┘
              │       │
              │  ┌────▼─────┐
              │  │ Compress │
              │  └────┬─────┘
              │       │
              │  ┌────▼─────┐
              └─►│   Base   │
                 └──────────┘
```

## 设计原则

### 高内聚
- 每个模块职责单一
- 相关功能聚合在一起
- 模块内部高度相关

### 低耦合
- 模块间通过接口交互
- SharedResources 依赖注入
- 易于单元测试和替换

### 可扩展
- 易于添加新功能
- 易于支持新 LLM 提供商
- 插件化架构

### 易维护
- 结构清晰
- 命名规范
- 文档完善

## 模块接口规范

### 命名空间
```cpp
namespace ben_gear {
    namespace agent { /* Agent 层 */ }
    namespace acp { /* ACP 统一协议层 */ }
    namespace config { /* Config 层 */ }
    namespace llm { /* LLM 层 */ }
    namespace tool { /* Tool 层 */ }
    namespace tools { /* Tools 层 */ }
    namespace skill { /* Skill 层 */ }
    namespace memory { /* Memory 层 */ }
    namespace workspace { /* Workspace 层 */ }
    namespace workflow { /* Workflow 层 */ }
    namespace mcp { /* MCP 层 */ }
    namespace cli { /* CLI 层 */ }
    namespace cli::render { /* 渲染层 */ }
    namespace cli::repl { /* REPL 层 */ }
    namespace net { /* Net 层 */ }
    namespace log { /* Log 层 */ }
    namespace base { /* Base 层 */ }
}
```

### 头文件组织
```cpp
#include "ben_gear/agent/agent.hpp"           // Agent 层
#include "ben_gear/agent/shared_resources.hpp" // 共享资源
#include "ben_gear/cli/args.hpp"               // CLI 解析器
#include "ben_gear/cli/render/cli_app.hpp"     // 渲染 + Agent 桥接
#include "ben_gear/cli/repl/chat_repl.hpp"     // 交互式 REPL
#include "ben_gear/config/loader.hpp"          // Config 层
#include "ben_gear/llm/provider_client.hpp"     // LLM 层
#include "ben_gear/llm/retry.hpp"              // LLM 重试
#include "ben_gear/llm/stream.hpp"             // 流式处理
#include "ben_gear/tool/registry.hpp"          // Tool 层
#include "ben_gear/skill/skill.hpp"            // Skill 层
#include "ben_gear/memory/store.hpp"           // Memory 层
#include "ben_gear/memory/compactor.hpp"       // Memory 压缩器
#include "ben_gear/memory/context.hpp"         // Memory 上下文构建器
#include "ben_gear/session/history_db.hpp"     // Session 层
#include "ben_gear/workspace/manager.hpp"      // Workspace 层
#include "ben_gear/workspace/session.hpp"      // Workspace Session
#include "ben_gear/mcp/mcp_client.hpp"         // MCP 层
#include "ben_gear/base/net/http.hpp"          // Net 层
#include "ben_gear/base/log/logger.hpp"        // Log 层
```

### 依赖规则
1. **单向依赖**：上层依赖下层，下层不依赖上层
2. **接口隔离**：通过接口交互，不暴露实现细节
3. **最小依赖**：只依赖必要的模块
4. **SharedResources 模式**：通过 shared_ptr 共享只读资源，避免重复构造

## 扩展指南

### 添加新 LLM 提供商
1. 在 `llm/` 目录添加新客户端
2. 实现 `chat_with_tools_async` / `chat_stream_with_tools_async`
3. 添加协议适配器（`to_xxx_format` / `from_xxx`）
4. 添加流解析器
5. 扩展 `ProviderClient` dispatch
6. 添加测试

### 添加新工具
1. 在 `tools/` 目录添加工具实现
2. 使用 `tool::registry.register_tool()` 注册
3. 定义 JSON Schema 参数
4. 在 `register_all_tools` 中调用注册
5. 添加单元测试

### 添加新技能
1. 创建技能目录 `~/.bengear/skills/<name>/`
2. 编写 SKILL.md（frontmatter key: value + Markdown 指令）
3. 运行 `--list-skills` 验证发现

### 添加新角色
1. 在工作空间的 `roles/` 目录创建 JSON 文件
2. 定义 `name`、`description`、`tool_whitelist`

### 添加新 MCP 服务器
1. 在 config.json 的 `mcp_servers` 添加服务器配置
2. 运行 Agent 自动连接并注册工具

### 添加新日志输出
1. 在 `base/log/` 目录添加新 Sink
2. 实现 `Sink` 接口
3. 注册到 Logger

## 最佳实践

1. **模块边界清晰**：不在模块间共享内部实现
2. **SharedResources 模式**：通过 shared_ptr 共享，避免重复构造
3. **Session 隔离**：每个 Session 独占可变状态，无需加锁
4. **接口稳定**：公共接口保持向后兼容
5. **文档完善**：每个公共接口都有文档
6. **测试覆盖**：每个模块都有单元测试
7. **性能优化**：关键路径有性能测试
8. **日志规范**：异常路径 log::error_fmt，正常关键节点 log::info_fmt
 原生 HTTP/HTTPS（TlsEngine 抽象，MbedTLS/OpenSSL/Schannel 多后端）
 压缩抽象（CompressEngine，zlib 后端）
 自研轻量测试框架（零 gtest/gmock/glog 依赖）
 namespace net { /* TLS 层 */ }
 namespace net { /* Compress 层 */ }
 namespace test { /* Test 层 */ }
#include "ben_gear/base/net/tls/tls_engine.hpp"  // TLS 引擎
#include "ben_gear/base/compress/compress_engine.hpp"  // 压缩引擎
#include "ben_gear/test/test_framework.hpp"      // 测试框架
