# 模块架构文档

## 模块结构

```
ben_gear/
├── agent/               # Agent 编排层
│   ├── agent.hpp        # Agent 主类
│   └── callbacks.hpp    # 回调接口
│
├── config/              # 配置管理层
│   ├── loader.hpp       # 配置加载
│   └── settings.hpp     # 配置定义
│
├── llm/                 # LLM 协议层
│   ├── anthropic_client.hpp        # Anthropic 客户端
│   ├── openai_client.hpp           # OpenAI 客户端
│   ├── provider_client.hpp         # 统一客户端接口
│   ├── chat.hpp                    # 聊天请求/响应
│   ├── http_helpers.hpp            # HTTP 辅助函数
│   ├── message.hpp                 # 统一消息格式
│   ├── retry.hpp                   # 重试机制
│   ├── stream.hpp                  # 流式响应
│   └── internal/                   # 内部实现
│       ├── anthropic_parser.hpp    # Anthropic 流解析器
│       ├── openai_parser.hpp       # OpenAI 流解析器
│       └── sse.hpp                 # SSE 解析
│
├── tool/                # 工具层
│   ├── types.hpp        # 工具类型定义
│   ├── registry.hpp     # 工具注册表
│   └── manager.hpp      # 工具调用管理
│
├── tools/               # 工具注册与实现
│   ├── builtin_tools.hpp   # 内置工具（文件/shell/http/扩展）
│   └── skill_tools.hpp     # 技能工具 + 技能管理工具
│
├── skill/               # 技能核心类型与逻辑
│   ├── skill.hpp        # 技能定义与加载器
│   └── zip_extract.hpp  # 下载与解压辅助
│
├── mcp/                 # MCP 协议层
│   ├── mcp_client.hpp   # MCP 客户端 + 管理器
│   └── mcp_config.hpp   # MCP 配置解析
│
├── base/                # 高性能基础组件层
│   ├── net/             # 网络层
│   │   ├── http.hpp         # 统一的 HTTP 客户端（内置连接池）
│   │   ├── connection_pool.hpp    # 连接池
│   │   ├── event_loop.hpp         # 事件循环
│   │   ├── socket.hpp             # Socket 封装
│   │   ├── task.hpp               # 协程任务
│   │   └── tcp_stream.hpp         # TCP 流
│   │
│   ├── log/             # 日志层
│   │   ├── logger.hpp       # 日志记录器
│   │   ├── sink.hpp         # 输出目标
│   │   ├── level.hpp        # 日志级别
│   │   └── configure.hpp    # 日志配置
│   │
│   ├── memory/          # 内存管理
│   │   └── pool.hpp     # 内存池
│   ├── concurrency/     # 并发组件
│   │   ├── thread_pool.hpp  # 线程池
│   │   └── lock_free.hpp    # 无锁数据结构
│   ├── container/       # 容器
│   │   ├── string.hpp   # 高性能字符串
│   │   ├── vector.hpp   # 动态数组
│   │   ├── map.hpp      # 哈希映射（支持 string_view 异构查找）
│   │   ├── format.hpp   # 格式化工具
│   │   └── object_pool.hpp  # 对象池
│   ├── io/              # I/O 组件
│   │   ├── buffer.hpp   # 高性能缓冲区
│   │   └── file.hpp     # 文件操作
│   ├── platform/        # 平台抽象
│   │   ├── platform.hpp # 平台接口（CPU、线程、进程、OS）
│   │   └── os.hpp       # 操作系统接口 + compat 兼容层 + subprocess 安全子进程
│   └── utils/           # 工具函数
│       ├── json.hpp     # JSON 工具
│       └── string_utils.hpp  # 字符串工具
│
└── ben_gear.hpp         # 主头文件
```

## 模块职责

### 1. Agent 层
**职责**：Agent 编排和对话管理

**核心功能**：
- Agent 主类实现
- 会话记忆管理
- 工具调用循环
- 回调通知机制

**依赖**：llm, tool, log, core

**设计原则**：
- 高内聚：只关注 Agent 编排逻辑
- 低耦合：通过接口依赖其他模块

### 2. Config 层
**职责**：配置加载和管理

**核心功能**：
- JSON 配置解析
- 多层配置合并
- 环境变量支持
- 配置验证

**依赖**：core

**设计原则**：
- 单一职责：只处理配置
- 易于扩展：支持新的配置源

### 3. LLM 层
**职责**：LLM 协议实现

**核心功能**：
- OpenAI 协议支持
- Anthropic 协议支持
- 流式响应解析
- 重试机制
- 统一消息格式

**依赖**：net, core, tool

**设计原则**：
- 协议适配：统一抽象不同 LLM 提供商
- 易于扩展：添加新提供商只需实现接口

### 4. Tool 层
**职责**：工具定义和管理

**核心功能**：
- 工具类型定义
- 工具注册表
- 工具调用管理
- JSON Schema 参数验证

**依赖**：core

**设计原则**：
- 高内聚：工具相关功能集中
- 低耦合：独立于 LLM 协议

### 5. Tools 层
**职责**：内置工具实现

**核心功能**：
- 文件工具（read/write/delete）
- 命令工具（run_command）
- HTTP 工具（http_get）
- 文件系统工具（list_dir/rename）

**依赖**：tool, core, net

**设计原则**：
- 可扩展：易于添加新工具
- 类型安全：使用 JSON Schema

### 6. Skill 层
**职责**：技能发现、加载和渐进式披露

**核心功能**：
- SKILL.md 解析（frontmatter key: value + Markdown）
- 全局/项目两级目录扫描
- 渐进式披露（3 级加载）
- get_skill 工具注册

**依赖**：tool, core

**设计原则**：
- 技能 ≠ 工具：技能是提示型知识包
- 懒加载：系统提示只注入元数据
- 后层覆盖：项目级覆盖全局级

### 7. MCP 层
**职责**：MCP 协议客户端

**核心功能**：
- stdio 传输（子进程 JSON-RPC）
- 自动发现 MCP 工具
- 工具执行路由
- 服务器生命周期管理

**依赖**：tool, core, net

**设计原则**：
- 透明集成：MCP 工具与内置工具无差别
- 配置驱动：通过 config.json 管理
- 安全可控：支持 disabled 标志

### 8. Net 层
**职责**：网络通信

**核心功能**：
- Socket 封装
- 事件循环
- TCP 连接
- HTTP 客户端
- 连接池

**依赖**：无（基础设施层）

**设计原则**：
- 异步 I/O：基于协程
- 跨平台：支持 Windows/Linux/macOS
- 高性能：连接复用

### 9. Log 层
**职责**：日志记录

**核心功能**：
- 异步日志
- 多输出目标（stdout/file/network）
- 日志级别
- 格式化输出

**依赖**：无（基础设施层）

**设计原则**：
- 异步写入：不阻塞主线程
- 线程安全：所有接口线程安全
- 可扩展：支持自定义输出目标

### 10. Core 层
**职责**：核心基础功能

**核心功能**：
- JSON 工具（解析、序列化）
- I/O 工具（文件读写）
- 字符串工具（转换、处理）
- 平台抽象（跨平台支持、安全子进程 subprocess::spawn）

**依赖**：无（最底层）

**设计原则**：
- 零依赖：不依赖其他模块
- 通用性：提供基础工具函数
- 跨平台：屏蔽平台差异

## 依赖关系

```
┌─────────────────────────────────────────┐
│              Application                 │
└─────────────────────────────────────────┘
                    │
        ┌───────────┴───────────┐
        │                       │
    ┌───▼────┐              ┌───▼────┐
    │ Agent  │              │ Config │
    └───┬────┘              └───┬────┘
        │                       │
    ┌───┴───────────────────────┴───┐
    │                               │
┌───▼────┐    ┌───────┐    ┌───▼────┐
│  LLM   │    │ Skill │    │  Tool  │
└───┬────┘    └───┬───┘    └───┬────┘
    │             │             │
    │       ┌─────┘       ┌─────┘
    │       │             │
    │   ┌───▼───┐    ┌───▼────┐
    │   │  MCP  │    │ Tools  │
    │   └───┬───┘    └────────┘
    │       │
    └───────►│
            ┌─▼───────┐
            │   Net    │
            └────┬─────┘
                 │
            ┌────▼─────┐
            │   Log    │
            └────┬─────┘
                 │
            ┌────▼─────┐
            │   Core   │
            └──────────┘
```

## 设计原则

### 高内聚
- 每个模块职责单一
- 相关功能聚合在一起
- 模块内部高度相关

### 低耦合
- 模块间通过接口交互
- 依赖注入设计
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
    // 顶层命名空间

    namespace agent { /* Agent 层 */ }
    namespace config { /* Config 层 */ }
    namespace llm { /* LLM 层 */ }
    namespace tool { /* Tool 层 */ }
    namespace tools { /* Tools 层 */ }
    namespace skill { /* Skill 层 */ }
    namespace mcp { /* MCP 层 */ }
    namespace net { /* Net 层 */ }
    namespace log { /* Log 层 */ }
    namespace core { /* Core 层 */ }
}
```

### 头文件组织
```cpp
// 每个模块有统一的主头文件
#include "ben_gear/agent/agent.hpp"         // Agent 层
#include "ben_gear/config/loader.hpp"       // Config 层
#include "ben_gear/llm/provider_client.hpp" // LLM 层
#include "ben_gear/tool/registry.hpp"       // Tool 层
#include "ben_gear/skill/skill.hpp"         // Skill 层
#include "ben_gear/mcp/mcp_client.hpp"      // MCP 层
#include "ben_gear/base/net/http.hpp"       // Net 层
#include "ben_gear/base/log/logger.hpp"     // Log 层
#include "ben_gear/base/json.hpp"           // Core 层
```

### 依赖规则
1. **单向依赖**：上层依赖下层，下层不依赖上层
2. **接口隔离**：通过接口交互，不暴露实现细节
3. **最小依赖**：只依赖必要的模块

## 扩展指南

### 添加新 LLM 提供商
1. 在 `llm/` 目录添加新客户端
2. 实现 `ProviderClient` 接口
3. 添加协议适配器
4. 注册到 `ProviderClient`

### 添加新工具
1. 在 `tools/` 目录添加工具实现
2. 使用 `tool::registry.register_tool()` 注册
3. 定义 JSON Schema 参数
4. 添加单元测试

### 添加新技能
1. 创建技能目录 `~/.bengear/skills/<name>/`
2. 编写 SKILL.md（frontmatter key: value + Markdown 指令）
3. 运行 `--list-skills` 验证发现

### 添加新 MCP 服务器
1. 在 config.json 的 `mcp_servers` 添加服务器配置
2. 运行 Agent 自动连接并注册工具

### 添加新日志输出
1. 在 `base/log/` 目录添加新 Sink
2. 实现 `Sink` 接口
3. 注册到 Logger

## 最佳实践

1. **模块边界清晰**：不在模块间共享内部实现
2. **接口稳定**：公共接口保持向后兼容
3. **文档完善**：每个公共接口都有文档
4. **测试覆盖**：每个模块都有单元测试
5. **性能优化**：关键路径有性能测试
