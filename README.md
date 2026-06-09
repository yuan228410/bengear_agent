# BenGear

BenGear 是一个从零开始用 C++20 构建的学习型 AI Agent 项目，不依赖任何现有的 Agent 框架。它旨在学习、研究和实践将 LLM 从"能对话的系统"转变为"能行动的系统"的机制。

该项目探索了大语言模型之上的层级，即 Agentic AI 的核心能力：工具调用、记忆管理、多模型协作、自主规划、流式推理、可观测性和原生网络。BenGear 采用逐层构建的方式，深入理解 Agent 系统中每个齿轮如何咬合并驱动整个机器运转。

## 特性

- ✅ **原生工具调用 API** - 支持 OpenAI 和 Anthropic 原生工具调用格式
- ✅ **多协议支持** - OpenAI Chat Completions 和 Anthropic Messages 协议
- ✅ **统一抽象** - ACP 统一协议 + 一套代码支持多个 LLM 提供商
- ✅ **流式响应** - 支持思考过程和工具调用回调，增量解析 streaming tool calls
- ✅ **交互式 REPL** - 行编辑、历史记录、`/` 命令自动补全、跨平台 raw mode
- ✅ **终端富文本** - Markdown 实时渲染（标题/表格/列表/代码高亮）、thinking 折叠、Dracula 主题
- ✅ **协程异步** - 基于 C++20 协程的异步 Agent/LLM/HTTP API
- ✅ **技能系统** - SKILL.md 渐进式披露，全局+项目两级加载
- ✅ **MCP 协议** - Model Context Protocol 客户端，stdio + HTTP 传输
- ✅ **三层级记忆** - MEMORY.md / SOUL.md / RULES.md 按 section merge
- ✅ **上下文压缩** - Compactor 压缩旧轮次为摘要，持久化缓存
- ✅ **会话持久化** - SQLite 存储会话历史，支持恢复、搜索、删除
- ✅ **工作流引擎** - DAG 任务编排、并行执行、LLM 子 Agent、命名空间隔离
- ✅ **计划模式** - 自动规划 / 手动规划，步骤化执行，工具拦截，动态提示符
- ✅ **连接池** - TLS 连接复用 + HTTP keep-alive + ObjectPool 减少堆分配 + 空闲超时淘汰 + 读空闲超时保护
- ✅ **高性能 JSON 解析器** - container::Json，递归下降解析 + SIMD 加速 + 两遍序列化，API 兼容 nlohmann/json
- ✅ **高性能基础组件** - container::String (SSO)、container::Map (开放寻址)、MemoryPool、无锁队列
- ✅ **跨平台** - macOS、Linux、Windows 支持

## 快速开始

### 系统要求

- CMake 3.20+
- C++20 编译器
- macOS、Linux 或 Windows
- OpenSSL（用于 HTTPS）
- zlib（用于压缩）

### 构建

```bash
cmake -S . -B build
cmake --build build
```

可选 CMake 标志：

```bash
cmake -S . -B build \
  -DBEN_GEAR_BUILD_TESTS=ON \
  -DBEN_GEAR_BUILD_EXAMPLES=ON \
  -DBEN_GEAR_BUILD_BENCHMARKS=ON
```

### 运行

```bash
# 交互式聊天（REPL 模式）
./build/bengear

# 单次提示
./build/bengear "你好，介绍一下 BenGear"

# 从 stdin 读取
cat prompt.txt | ./build/bengear --stdin

# 显示配置
./build/bengear --show-config
```

### 交互式 REPL

进入交互模式后支持：

| 功能 | 操作 |
|------|------|
| 行编辑 | ← → Home End Ctrl+A Ctrl+E |
| 删除 | Backspace Delete Ctrl+U Ctrl+K Ctrl+W |
| 历史浏览 | ↑ ↓ |
| 命令补全 | 输入 `/` 自动显示候选，Tab/Shift+Tab 切换 |
| 退出 | 连按两次 Ctrl+C |

内置 `/` 命令：`/help`、`/exit`、`/new`、`/sessions`、`/resume <id>`、`/plan`、`/approve`、`/skip`、`/cancel`、`/steps`、`/compact`、`/clear`、`/model`

### 配置

```bash
cp config-example.json config.json
# 编辑 config.json，填入 API 密钥和模型名称
```

**详细文档：[快速开始](docs/quickstart.md)**

## 文档

### 入门

- [快速开始](docs/quickstart.md) - 构建和运行
- [配置详解](docs/configuration.md) - 完整配置选项
- [CLI 参考](docs/cli.md) - 所有 CLI 选项、REPL 快捷键和 / 命令

### 核心功能

- [工具参考](docs/tools-reference.md) - 内置工具列表
- [记忆系统](docs/memory.md) - 三层级记忆和上下文压缩
- [技能系统](docs/skills.md) - 技能发现和加载
- [MCP 协议](docs/mcp.md) - 外部工具集成
- [工作空间](docs/workspace.md) - 工作空间管理
- [工作流引擎](docs/workflow_guide.md) - DAG 任务编排和执行
- [计划模式](docs/plan_mode.md) - 自动规划与步骤化执行

### 架构设计

- [架构概览](docs/architecture.md) - 系统架构和设计原则
- [模块架构](docs/module_architecture.md) - 模块划分和依赖
- [LLM 协议](docs/llm-protocols.md) - OpenAI/Anthropic 协议实现
- [网络设计](docs/networking.md) - HTTP 客户端和连接池
- [回调设计](docs/callbacks.md) - 事件回调接口

### 基础组件

- [基础组件](docs/base_components.md) - 容器、内存池、JSON 解析器、日志等
- [JSON 解析器设计](docs/json_parser_design.md) - 高性能 JSON 解析器架构
- [日志系统](docs/logging.md) - 异步日志和输出配置
- [测试指南](docs/testing.md) - 单元测试和性能测试

## 架构概览

### 核心模块

```text
├── agent/          # Agent 编排、回调、共享资源
├── acp/            # Agent Communication Protocol（统一消息/内容块/编解码/流式）
├── llm/            # LLM 协议实现（OpenAI/Anthropic + ACP 适配器）
├── tool/           # 工具注册和管理
├── tools/          # 内置工具实现
├── skill/          # 技能发现和加载
├── memory/         # 记忆存储和上下文压缩
├── workflow/       # 工作流引擎（DAG 调度、命名空间隔离、模板库）
├── workspace/      # 工作空间和会话管理
├── mcp/            # MCP 协议客户端
├── config/         # 配置加载
├── cli/            # CLI 解析器
│   ├── render/     #   终端渲染器（Markdown/主题/语法高亮/Spinner）
│   └── repl/       #   交互式行编辑器（REPL/历史/补全）
└── base/           # 基础组件（网络、日志、容器、内存池等）
```

### 设计原则

- **高内聚**：每个模块职责单一
- **低耦合**：通过接口交互，依赖注入
- **统一抽象**：一套代码支持多个提供商
- **可扩展**：易于添加新工具和新提供商

**详细文档：[架构设计](docs/architecture.md)**

## 许可证

MIT License
