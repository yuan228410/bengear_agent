# 模块架构文档

## 模块结构

```
ben_gear/
├── agent/                     # Agent 编排层
│   ├── agent.hpp              # Agent 主类（Session-based API，无状态调度器）
│   ├── callbacks.hpp          # 回调接口（on_token/on_thinking/on_tool_call/on_tool_result）
│   └── shared_resources.hpp   # 共享资源（一次构建，多 Agent/多会话复用）
│
├── cli/                       # 命令行解析
│   └── args.hpp               # 声明式 CLI 解析器（子命令 + 链式 API + 自动帮助）
│
├── config/                    # 配置管理层
│   ├── loader.hpp             # 配置加载
│   └── settings.hpp           # 配置定义（model_config 分组格式）
│
├── llm/                       # LLM 协议层
│   ├── anthropic_client.hpp   # Anthropic 客户端
│   ├── openai_client.hpp      # OpenAI 客户端
│   ├── provider_client.hpp    # 统一客户端接口（协议分发边界）
│   ├── chat.hpp               # 聊天请求/响应
│   ├── http_helpers.hpp       # HTTP 辅助函数
│   ├── message.hpp            # 统一消息格式 + ContentBlock
│   ├── retry.hpp              # 重试机制（同步 + 异步 + HTTP 重试）
│   ├── stream.hpp             # 流式响应（StreamHandlers + StreamToolCallDelta）
│   └── internal/              # 内部实现
│       ├── anthropic_parser.hpp  # Anthropic 流解析器
│       ├── openai_parser.hpp     # OpenAI 流解析器
│       └── sse.hpp               # SSE 解析
│
├── tool/                      # 工具层
│   ├── types.hpp              # 工具类型定义
│   ├── registry.hpp           # 工具注册表（线程安全，shared_mutex）
│   └── manager.hpp            # 工具调用管理器
│
├── tools/                     # 工具注册与实现
│   ├── builtin_tools.hpp      # 内置工具（文件 10 个/shell 1 个/http 2 个/搜索 2 个）
│   ├── skill_tools.hpp        # 技能工具 + get_skill + 5 个管理工具
│   ├── memory_tools.hpp       # 记忆工具（7 个：读写记忆/灵魂/规范 + recall + episode）
│   └── workspace_tools.hpp    # 工作空间工具（4 个：列表/创建/删除/恢复）
│
├── skill/                     # 技能核心类型与逻辑
│   ├── skill.hpp              # 技能定义与加载器
│   └── zip_extract.hpp        # 下载与解压辅助
│
├── memory/                    # 记忆系统
│   ├── store.hpp              # 记忆存储（MemoryStore，跨进程文件锁 + 原子写入）
│   ├── episode.hpp            # 剧集存储（EpisodeStore，每日情景 + FileLock 安全追加）
│   ├── context.hpp            # 上下文构建器（ContextBuilder，7 步组装 + CJK token 估算）
│   ├── compactor.hpp          # 上下文压缩器（Compactor，软/硬阈值 + 持久化缓存）
│   ├── updater.hpp            # 记忆更新器（MemoryUpdater，LLM 驱动 + 重试 + 标签提取）
│   ├── section_merge.hpp      # 章节合并（merge_sections，last-wins）
│   └── types.hpp              # 记忆类型定义（MemoryKind, MergedMemory）
│
├── role/                      # 角色系统
│   ├── loader.hpp             # 角色加载器（RoleLoader，三层级扫描）
│   ├── filter.hpp             # 工具过滤器（ToolFilter，组合模式 + 白名单）
│   └── types.hpp              # 角色类型定义（RoleDefinition）
│
├── session/                   # 会话持久化
│   ├── history_db.hpp         # 历史数据库（HistoryDB，SQLite + FTS5）
│   └── uuid.hpp               # UUID v4 生成
│
├── workspace/                 # 工作空间管理
│   ├── manager.hpp            # 工作空间管理器（WorkspaceManager，CRUD + 软删除/恢复）
│   ├── session.hpp            # 会话管理（Session，独占 history/Compactor/MemoryUpdater）
│   └── types.hpp              # 工作空间类型定义（WorkspaceContext, TierPaths, WorkspaceMeta）
│
├── mcp/                       # MCP 协议层
│   ├── mcp_client.hpp         # MCP 客户端 + 管理器（stdio + HTTP 双传输 + ThreadPool 并行）
│   └── mcp_config.hpp         # MCP 配置解析
│
├── base/                      # 高性能基础组件层
│   ├── net/                   # 网络层
│   │   ├── http.hpp           # 统一的 HTTP 客户端（内置连接池 + ObjectPool）
│   │   ├── connection_pool.hpp # 连接池（预热 + shared_mutex 读写锁）
│   │   ├── event_loop.hpp     # 事件循环
│   │   ├── socket.hpp         # Socket 封装
│   │   ├── task.hpp           # 协程任务
│   │   └── tcp_stream.hpp     # TCP 流
│   │
│   ├── log/                   # 日志层
│   │   ├── logger.hpp         # 日志记录器（前端轻量采集 + 后端异步格式化）
│   │   ├── sink.hpp           # 输出目标（Stdout / File 轮转 / TCP Server）
│   │   ├── level.hpp          # 日志级别
│   │   └── configure.hpp      # 日志配置
│   │
│   ├── memory/                # 内存管理
│   │   └── pool.hpp           # 内存池（PoolStats 原子字段 + STL 兼容分配器）
│   │
│   ├── concurrency/           # 并发组件
│   │   ├── thread_pool.hpp    # 线程池（工作窃取 + 动态调整）
│   │   └── lock_free.hpp      # 无锁数据结构（Queue/Stack/RingBuffer）
│   │
│   ├── container/             # 容器
│   │   ├── string.hpp         # 高性能字符串（SSO + hash 委托 string_view + find 用 std::search）
│   │   ├── vector.hpp         # 动态数组（支持自定义分配器）
│   │   ├── map.hpp            # 哈希映射（开放寻址法 + 罗宾汉哈希 + 异构查找）
│   │   ├── format.hpp         # 格式化工具
│   │   └── object_pool.hpp    # 对象池（FixedSizePool + free list）
│   │
│   ├── io/                    # I/O 组件
│   │   ├── buffer.hpp         # 高性能缓冲区
│   │   └── file.hpp           # 文件操作
│   │
│   ├── platform/              # 平台抽象
│   │   ├── platform.hpp       # 平台接口（CPU、线程、进程、OS）
│   │   ├── os.hpp             # 操作系统接口 + compat 兼容层 + subprocess 安全子进程 + FileLock
│   │   └── file_lock.hpp      # 跨平台文件锁（POSIX fcntl + Windows LockFileEx）
│   │
│   └── utils/                 # 工具函数
│       ├── json.hpp           # JSON 工具
│       └── string_utils.hpp   # 字符串工具（to_lower/trim 等）
│
└── ben_gear.hpp               # 主头文件
```

## 模块职责

### 1. Agent 层
**职责**：Agent 编排和会话调度

**核心功能**：
- Session-based 对话管理（Agent 无状态，Session 独占 history）
- 流式/非流式双路径
- 流式增量工具调用解析
- 工具调用循环（max_tool_steps 限制）
- 回调通知机制
- 记忆压缩（Compactor）
- 角色过滤（ToolFilter）
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
- `ToolFilter` — 角色过滤器

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

### 9. 角色系统
**职责**：基于白名单的工具过滤

**核心类**：
- `RoleDefinition` — 角色定义 + is_tool_allowed
- `RoleLoader` — 三层级扫描
- `ToolFilter` — 组合模式（to_openai_tools / to_anthropic_tools / filtered_registry）

### 10. 网络层
**职责**：网络通信

**核心功能**：
- 原生 HTTP/HTTPS（OpenSSL TLS）
- 连接池（shared_mutex + ObjectPool）
- 协程异步（EventLoop + TcpStream）
- URL 解析器 + HTTP/1.1 请求构建
- Chunked transfer 解码
- 预热支持

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
    namespace config { /* Config 层 */ }
    namespace llm { /* LLM 层 */ }
    namespace tool { /* Tool 层 */ }
    namespace tools { /* Tools 层 */ }
    namespace skill { /* Skill 层 */ }
    namespace memory { /* Memory 层 */ }
    namespace role { /* Role 层 */ }
    namespace session { /* Session 层 */ }
    namespace workspace { /* Workspace 层 */ }
    namespace mcp { /* MCP 层 */ }
    namespace cli { /* CLI 层 */ }
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
#include "ben_gear/config/loader.hpp"          // Config 层
#include "ben_gear/llm/provider_client.hpp"     // LLM 层
#include "ben_gear/llm/retry.hpp"              // LLM 重试
#include "ben_gear/llm/stream.hpp"             // 流式处理
#include "ben_gear/tool/registry.hpp"          // Tool 层
#include "ben_gear/skill/skill.hpp"            // Skill 层
#include "ben_gear/memory/store.hpp"           // Memory 层
#include "ben_gear/memory/compactor.hpp"       // Memory 压缩器
#include "ben_gear/memory/context.hpp"         // Memory 上下文构建器
#include "ben_gear/role/loader.hpp"            // Role 层
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
3. 通过 `--role` 指定角色

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
