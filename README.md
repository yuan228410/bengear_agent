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
- ✅ **Provider 故障转移** - 多 API key 轮转 + fallback chain + 会话粘性 + 指数退避冷却
- ✅ **上下文轻量裁剪** - ContextPruner 工具结果软/硬裁剪，与 Compactor 互补
- ✅ **Token 用量统计** - 按会话+全局双维度追踪，atomic 无锁热路径
- ✅ **混合记忆检索** - BM25 关键词检索 + 时间衰减评分，纯本地无外部依赖
- ✅ **安全沙箱** - 路径白名单 + 命令黑名单 + 正则匹配，防止 LLM 生成危险操作
- ✅ **请求限流** - 滑动窗口 + 突发控制，保护 API 配额
- ✅ **Hook 系统** - 10 个扩展点，void/modifying/sync 三模式，插件化扩展
- ✅ **通用工具** - DEFER (Go 风格 RAII)、BG_TRY_ASSIGN (Rust ? 风格)、Noncopyable
- ✅ **上下文压缩** - Compactor 压缩旧轮次为摘要，持久化缓存
- ✅ **会话持久化** - SQLite 存储会话历史，支持恢复、搜索、删除
- ✅ **工作流引擎** - DAG 任务编排、并行执行、LLM 子 Agent、命名空间隔离
- ✅ **子 Agent 委派** - 主 Agent 通过 tool call 委派任务给子 Agent，独立 EventLoop + ReAct 循环执行多步工具调用，无系统提示词（上下文由主 Agent 提供），工具隔离（exclude_tools 黑名单），自定义子 Agent（.md 文件驱动），output/full_output 两级输出
- ✅ **计划模式** - 自动规划 / 手动规划，步骤化执行，工具拦截，动态提示符
- ✅ **连接池** - TLS 连接复用 + HTTP keep-alive + ObjectPool 减少堆分配 + 空闲超时淘汰 + 读空闲超时保护
- ✅ **高性能 JSON 解析器** - container::Json，递归下降解析 + SIMD 加速 + 两遍序列化，API 兼容 nlohmann/json
- ✅ **高性能基础组件** - 标准库容器、MemoryPool、无锁队列
- ✅ **跨平台** - macOS、Linux、Windows 支持
- ✅ **会话类型区分** - 主会话/子 Agent 会话/工作流会话，parent_id 关联，按类型过滤查询
- ✅ **Server 模式** - HTTP/WS 双协议服务，WebSocket 双向通信，REST API，Bearer Token 认证，SessionPool LRU 管理
- ✅ **Web 前端** - Vite + Vue 3，左侧工作空间/会话导航、聊天区、右侧可折叠 TODO 面板、多主题切换
- ✅ **Web 计划模式** - 结构化计划草稿、方案选择、用户修订、条目编辑、确认执行与会话恢复
- ✅ **执行 TODO 面板** - LLM 驱动任务拆解和进度更新，按 workspace/session 隔离并持久化，普通工具调用不污染 TODO
- ✅ **执行事件可视化** - sub-agent/team/task/tool 统一事件树，工具调用默认折叠，支持运行终态和继续建议

## 快速开始

### 系统要求

- CMake 3.20+
- C++20 编译器
- macOS、Linux 或 Windows
- OpenSSL（用于 HTTPS）
- zlib（用于压缩）

### 构建

```bash
# 1. 初始化 submodule（首次克隆后必须执行）
git submodule update --init --recursive

# 2. 配置和编译
cmake -S . -B build
cmake --build build
```

> **Windows (w64devkit) 用户**：建议使用 `./build_mingw.sh` 一键编译，详见下方 [MinGW64 章节](#mingw64windows)。

#### 高效编译（并行构建）

项目已针对多核编译做了优化，不同平台参数如下：

| 平台 / 工具链 | 并行参数 | 说明 |
|--------------|---------|------|
| MSVC（Visual Studio） | `/MP` | 已在 CMake 中默认开启，编译器级多文件并行（`add_compile_options(/MP)`） |
| MSVC（构建时） | `--parallel` | 顶层构建并行，配合 `/MP` 效果最佳 |
| Ninja | 自动 | 默认按 CPU 核数并行，无需额外参数 |
| Make / MinGW Makefiles | `-j[N]` | 按 N 路并行，`-j` 不带数字则不限制 |

```bash
# MSVC / Visual Studio 生成器：/MP 已默认开启，构建时再加 --parallel
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build --config Release --parallel

# Ninja：默认已并行
cmake -S . -B build -G Ninja
cmake --build build

# MinGW (w64devkit)：使用 build_mingw.sh 或手动编译
./build_mingw.sh
```

> 低内存环境请降低并行度（如 `-j2`）或改用单线程预设 `--preset dev`，避免 OOM。

#### MinGW64（Windows）

安装 [w64devkit](https://github.com/skeeto/w64devkit) 后，将 w64devkit 的 bin 目录加入 PATH。

> **注意**：w64devkit 自带 Ninja、GCC 14、CMake，无需额外安装。

**一键编译（推荐）**

```bash
# 首次克隆后
git submodule update --init --recursive
./build_mingw.sh

# 指定构建类型
./build_mingw.sh Release
```

脚本自动处理 MinGW + Ninja 的兼容性问题（`.obj` 扩展名和 `ar` 索引）。

**手动编译**

```bash
git submodule update --init --recursive

# 配置（使用 MinGW toolchain 文件）
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_TOOLCHAIN_FILE=mingw_toolchain.cmake \
  -DBEN_GEAR_BUILD_EXAMPLES=OFF \
  -DBEN_GEAR_BUILD_BENCHMARKS=OFF

# 后处理：修复 .obj 扩展名问题（部分安全软件会拦截 .obj 文件）
sed -i 's/\.cpp\.obj\b/.cpp.o/g; s/\.c\.obj\b/.c.o/g' build/build.ninja
sed -i 's/\.cpp\.obj\.d\b/.cpp.o.d/g; s/\.c\.obj\.d\b/.c.o.d/g' build/build.ninja
sed -i 's/gcc-ar qc/gcc-ar crs/g; s/&& gcc-ranlib $TARGET_FILE //g' build/CMakeFiles/rules.ninja

# 编译
cmake --build build -j20
```

**TLS 后端**

默认使用 MbedTLS（vendor），CA 证书包 `third_party/cacert.pem` 编译时自动复制到 exe 目录。也可使用 Schannel（Windows 原生 TLS，使用系统证书存储，无需 CA 文件）：

```bash
# 在 cmake 配置时添加
-DTLS_BACKEND=schannel
```

**运行测试**

```bash
cmake --build build --target run_tests -j20
```

也可以使用预设构建（默认单线程，适合低内存环境）：

```bash
cmake --preset dev
cmake --build --preset dev-bengear
```

可选 CMake 标志：

```bash
cmake -S . -B build \
  -DBEN_GEAR_BUILD_TESTS=ON \
  -DBEN_GEAR_BUILD_EXAMPLES=ON \
  -DBEN_GEAR_BUILD_BENCHMARKS=ON
```

低内存环境建议关闭示例/基准并单线程构建：

```bash
cmake --preset dev
cmake --build --preset dev-bengear
```

生命周期和泄漏相关变更建议至少运行：

```bash
cmake --build --preset dev-tests
./build-dev/bengear_tests --filter LifecycleTest.*
```

#### 构建类型

```bash
# Debug（完整调试符号，崩溃可看行号）
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug

# Release（无调试符号，体积最小性能最好）
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# RelWithDebInfo（优化 + 调试符号，推荐日常开发）
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
```

#### 依赖说明

项目使用 git submodule 管理第三方依赖：

| 依赖 | 版本 | 用途 |
|------|------|------|
| Mbed TLS | 3.6.2 | TLS 加密（HTTPS） |
| zlib | 1.3.1 | 数据压缩 |

> **注意**：Mbed TLS 和 zlib 已内置，无需系统安装。如需使用系统 OpenSSL 替代 Mbed TLS，可添加 `-DTLS_BACKEND=openssl`。

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

# 启动 HTTP/WebSocket Server
./build/bengear serve
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

- [工具参考](docs/tools.md) - 内置工具列表
- [子 Agent 系统](docs/sub_agent.md) - 子 Agent 委派、并行执行、推测执行
- [三种运行模式](docs/design_three_modes.md) - Single Agent / Multi-Agent / Server
- [Server 模式](docs/server_mode.md) - HTTP/WebSocket 服务、Web UI、会话状态与执行事件
- [Web 计划模式与执行 TODO](docs/web_plan_todo.md) - 计划审阅、TODO 面板、继续执行与验证场景
- [记忆系统](docs/memory.md) - 三层级记忆和上下文压缩
- [技能系统](docs/skills.md) - 技能发现和加载
- [MCP 协议](docs/mcp.md) - 外部工具集成
- [工作空间](docs/workspace.md) - 工作空间管理
- [配置详解](docs/configuration.md) - 故障转移、安全沙箱、限流、Hook 等
- [Web 计划模式与执行 TODO](docs/web_plan_todo.md) - 计划审阅、TODO、执行事件

### 架构设计

- [架构概览](docs/architecture.md) - 系统架构和设计原则
- [模块架构](docs/module_architecture.md) - 模块划分和依赖
- [ACP 协议](docs/acp.md) - Agent Communication Protocol

### 基础组件

- [基础组件](docs/base_components.md) - 容器、内存池、JSON 解析器、日志等
- [JSON 解析器设计](docs/json_parser_design.md) - 高性能 JSON 解析器架构
- [测试指南](docs/testing.md) - 单元测试和性能测试

## 架构概览

### 核心模块

```text
src/
├── acp/                    # Agent Communication Protocol（统一消息/内容块/编解码/流式/适配器）
├── agent/
│   ├── core/               # 事件类型（TokenEvent/ToolCallEvent 等）+ 事件接口（I*EventSink）+ 子 Agent 配置
│   ├── execution/          # 执行原语层（ExecutionLoop + IInterceptor 链）
│   └── runtime/            # 完整运行时（ServiceRegistry 管理全部子服务 + SubAgentRuntime + LifecycleManager + application/）
├── base/                   # 基础组件（网络/日志/容器/内存池/JSON/压缩/并发/平台/配置/IO
│   └── core/               #   ServiceRegistry / EventBus / IMetricsCollector / ITracer）
├── capabilities/           # 能力抽象层
│   ├── tool/               #   工具注册表 + 类型 + 管理器 + 内置工具
│   ├── skill/              #   技能发现和加载
│   ├── mcp/                #   MCP 协议客户端
│   ├── git/                #   Git 能力
│   └── patch/              #   补丁能力
├── cli/                    # CLI 解析器
│   ├── render/             #   终端渲染器（Markdown/主题/语法高亮）
│   └── repl/               #   交互式行编辑器（REPL/历史/补全）
├── domain/                 # 领域事件与错误（纯基础类型，不依赖 tool/llm）
├── llm/                    # LLM 协议实现（ProviderClient + IProviderClient + OpenAI/Anthropic）
├── memory/                 # 记忆存储、上下文构建、压缩、裁剪 + 工具注册
├── orchestration/          # 计划、TODO、执行事件序列化
├── plugins/                # 插件加载器（.dll/.so C ABI）
├── server/
│   ├── api/                #   REST API（会话/配置/MCP/文件）— 虚基类接口
│   ├── callback/           #   事件回调和序列化
│   ├── composition/        #   服务组合层
│   ├── core/               #   Server 核心 + HTTP 路由 + EventBridge（EventBus → WebSocket）
│   ├── http/               #   HTTP 解析 + 静态文件
│   ├── session/            #   Session 池（LRU）
│   └── ws/                 #   WebSocket 处理 + WsSessionManager
├── team/                   # 多 Agent 协作框架（长活 Agent、黑板、消息队列、pipeline）
└── workspace/              # 工作空间、会话历史（SQLite）、状态持久化 + 工具注册
```

### 设计原则

- **高内聚**：每个模块职责单一
- **低耦合**：通过接口交互，依赖注入
- **ServiceRegistry**：所有服务通过 `services().resolve<T>()` 统一访问，无直接 accessor
- **EventBus**：Agent 事件通过发布/订阅模式解耦，替代旧回调链
- **统一抽象**：ACP 统一协议 + IProviderClient 虚基类
- **可扩展**：工具在各领域模块自注册，新增 Provider 只需实现接口

**详细文档：[架构设计](docs/architecture.md)**

## 许可证
