# BenGear

BenGear 是一个从零开始用 C++20 构建的学习型 AI Agent 项目，不依赖任何现有的 Agent 框架。它旨在学习、研究和实践将 LLM 从"能对话的系统"转变为"能行动的系统"的机制。

该项目探索了大语言模型之上的层级，即 Agentic AI 的核心能力：工具调用、记忆管理、多模型协作、自主规划、流式推理、可观测性和原生网络。BenGear 采用逐层构建的方式，深入理解 Agent 系统中每个齿轮如何咬合并驱动整个机器运转。

目前提供了一个小型 CLI 和框架，用于实验 LLM 驱动的编码工作流，具有模块化协议支持、可扩展工具、可观测执行和低耦合架构，可演进为高性能原生网络栈。

## 特性

- ✅ **原生工具调用 API** - 支持 OpenAI 和 Anthropic 原生工具调用格式
- ✅ **多协议支持** - OpenAI Chat Completions 和 Anthropic Messages 协议
- ✅ **统一抽象** - 一套代码支持多个 LLM 提供商
- ✅ **流式响应** - 支持思考过程和工具调用回调
- ✅ **协程异步** - 基于 C++20 协程的异步 Agent/LLM/HTTP API
- ✅ **技能系统** - SKILL.md 渐进式披露，全局+项目两级加载
- ✅ **MCP 协议** - Model Context Protocol 客户端，自动发现外部工具
- ✅ **内置工具** - 文件读写、命令执行、HTTP 请求等 8 个工具
- ✅ **JSON Schema** - 工具参数使用 JSON Schema 定义，类型安全
- ✅ **会话记忆** - 支持多轮对话上下文保持
- ✅ **连接池** - HTTP 连接复用，提升性能
- ✅ **可观测性** - 异步日志、stdout、文件、网络输出
- ✅ **跨平台** - macOS、Linux、Windows 支持
- ✅ **完整测试** - 单元测试、示例、性能测试

## 系统要求

- CMake 3.20+
- C++20 编译器
- macOS、Linux 或 Windows
- OpenSSL（用于 HTTPS）

原生 HTTP/HTTPS 请求直接使用 BenGear 的 socket 传输层，无需 curl 等外部工具。

## 构建

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

## 测试

```bash
ctest --test-dir build --output-on-failure
# 或直接运行
./build/bengear_tests
```

## 运行

交互式聊天：

```bash
./build/bengear
```

单次提示：

```bash
./build/bengear "你好，介绍一下 BenGear"
```

从 stdin 读取提示：

```bash
cat prompt.txt | ./build/bengear --stdin
```

显示配置：

```bash
./build/bengear --show-config
```

运行时覆盖模型：

```bash
./build/bengear --active-model oneapi-claude-sonnet "hello"
```

## 配置

主配置文件为工作区根目录的 `config.json`。此文件被 Git 忽略，因为它可能包含密钥。使用 `config-example.json` 作为模板。

顶层字段：

- `active_model`: 选中的模型键名
- `stream`: 全局流式开关，默认 `true`
- `log`: 结构化日志配置
- `llm_request_retry`: LLM HTTP 请求重试策略
- `agent`: Agent 行为配置（max_tool_steps, system_prompt, command_timeout）
- `connection_pool`: HTTP 连接池配置
- `thread_pool`: 线程池配置
- `mcp`: MCP 协议配置
- `models`: 命名模型定义
- `mcp_servers`: MCP 服务器定义

示例配置：

```json
{
  "active_model": "my-openai-model",
  "stream": true,
  "log": {
    "level": "info",
    "output": "file"
  },
  "llm_request_retry": {
    "max_attempts": 5,
    "initial_delay_ms": 200,
    "max_delay_ms": 3000
  },
  "models": {
    "my-openai-model": {
      "api_mode": "openai",
      "api_key": "${YOUR_API_KEY}",
      "base_url": "https://api.openai.com",
      "model": "gpt-4",
      "temperature": 0.3
    },
    "my-anthropic-model": {
      "api_mode": "anthropic",
      "api_key": "${YOUR_API_KEY}",
      "base_url": "https://api.anthropic.com",
      "model": "claude-3-5-sonnet-20241022",
      "temperature": 0.3
    }
  }
}
```

从示例开始：

```bash
cp config-example.json config.json
```

然后编辑 `config.json`，填入本地 API 密钥和模型名称。

`base_url` 支持自动协议路径补全：

- OpenAI: 自动追加 `/v1/chat/completions`
- Anthropic: 自动追加 `/v1/messages`
- 如果 `base_url` 已以 `/v1` 结尾，不会重复添加
- 如果设置了 `api_url`，则使用完整端点

## CLI 选项

```text
--config <path>                 JSON 模型配置路径
--active-model <name>           模型键名
--provider openai|anthropic     覆盖提供商协议
--model <name>                  覆盖模型名称
--base-url <url>                覆盖基础 URL
--api-url <url>                 覆盖完整 API URL
--api-key <key>                 覆盖 API 密钥
--workspace <path>              工作区根目录
--chat                          强制交互式聊天
--stream                        强制流式
--no-stream                     禁用流式
--async                         使用异步 Agent API
--sync                          使用同步兼容 API
--show-config                   打印解析后的配置
--list-skills                   列出已发现的技能
```

## 工具系统

### 原生工具调用

BenGear 使用 OpenAI 和 Anthropic 的原生工具调用 API，而非手动解析 JSON。

**OpenAI 格式：**
```json
{
  "tools": [{
    "type": "function",
    "function": {
      "name": "read_file",
      "description": "读取文件内容",
      "parameters": {
        "type": "object",
        "properties": {
          "path": {"type": "string", "description": "文件路径"}
        },
        "required": ["path"]
      }
    }
  }]
}
```

**Anthropic 格式：**
```json
{
  "tools": [{
    "name": "read_file",
    "description": "读取文件内容",
    "input_schema": {
      "type": "object",
      "properties": {
        "path": {"type": "string", "description": "文件路径"}
      },
      "required": ["path"]
    }
  }]
}
```

### 内置工具

当前提供 8 个内置工具：

**文件工具：**
- `read_file` - 读取文件内容，支持行范围
- `write_file` - 写入文件，支持覆盖/追加模式
- `delete_file` - 删除文件或目录

**命令工具：**
- `run_command` - 执行 shell 命令并返回输出

**HTTP 工具：**
- `http_get` - HTTP GET 请求

**文件系统工具：**
- `list_dir` - 列出目录内容，支持递归
- `rename_file` - 重命名或移动文件/目录

### 自定义工具

注册自定义工具：

```cpp
agent.register_tool(
    "my_tool",
    "工具描述",
    {
        {"param1", ToolParameterSchema{
            .type = "string",
            .description = "参数1说明"
        }},
        {"param2", ToolParameterSchema{
            .type = "number",
            .description = "参数2说明"
        }}
    },
    [](const Json& args) -> std::string {
        // 工具实现
        return "结果";
    }
);
```

## 架构设计

### 核心模块

```
include/ben_gear/
├── json.hpp                    # JSON 工具函数
├── llm/
│   ├── tool_types.hpp          # 工具类型定义
│   ├── tool_registry.hpp       # 工具注册表
│   ├── tool_call_manager.hpp   # 工具调用管理
│   ├── message.hpp             # 统一消息格式
│   ├── openai_client.hpp       # OpenAI 客户端
│   ├── anthropic_client.hpp    # Anthropic 客户端
│   └── provider_client.hpp     # 统一接口
├── skill/
│   ├── skill.hpp               # 技能定义与加载器
│   └── zip_extract.hpp         # 下载与解压辅助
├── mcp/
│   ├── mcp_client.hpp          # MCP 客户端 + 管理器
│   └── mcp_config.hpp          # MCP 配置解析
├── tools/
│   ├── builtin_tools.hpp       # 内置工具（文件/shell/http/扩展）
│   └── skill_tools.hpp         # 技能工具 + 技能管理工具
├── agent/
│   └── agent.hpp               # Agent 实现
├── config/                     # 配置管理
├── log/                        # 日志系统
└── net/                        # 网络层
```

### 设计原则

**高内聚：**
- 每个模块职责单一
- 工具定义与执行分离
- 协议适配器独立

**低耦合：**
- 通过接口交互
- 依赖注入设计
- 易于单元测试

**统一抽象：**
- 一套代码支持多个提供商
- 消息格式统一
- 工具调用流程统一

**可扩展：**
- 易于添加新工具
- 易于支持新 LLM 提供商
- 插件化架构

### 工作流程

```text
用户输入
  → Agent 接收
  → 构建消息历史
  → 调用 LLM (带工具定义)
  → LLM 返回工具调用请求
  → ToolCallManager 提取调用
  → ToolRegistry 执行工具
  → 构建工具结果消息
  → 继续调用 LLM
  → 返回最终结果
```

## 可观测性

终端输出显示：

- `[thinking] ... [/thinking]` - 模型思考过程
- `[tool call] ...` - 工具调用
- `[tool result] ...` - 工具执行结果

回调 API 设计用于将事件路由到 UI 面板、日志、WebSocket 流或追踪系统。

## 性能优化

- **连接池**：HTTP 连接复用，减少 TCP 握手开销
- **异步 I/O**：基于协程的异步网络
- **零拷贝**：尽可能避免数据复制
- **内存池**：未来可添加自定义内存管理

## 安全注意事项

- ⚠️ 不要提交真实 API 密钥
- ⚠️ 使用本地忽略的配置文件或环境变量存储凭证
- ⚠️ `run_command` 和 `write_file` 功能强大，暴露给不受信任用户前需添加策略层：
  - 允许的目录
  - 命令白名单/黑名单
  - 用户确认
  - 执行超时
  - 输出限制
  - 审计日志

## 项目结构

```text
include/ben_gear/agent/     Agent 编排、工具、回调
include/ben_gear/config/    配置设置和分层加载器
include/ben_gear/llm/       提供商客户端、协议体、流解析器
include/ben_gear/llm/internal/  内部实现（解析器、SSE）
include/ben_gear/tool/      工具类型、注册表、管理器
include/ben_gear/tools/     内置工具 + 技能工具 + 技能管理工具
include/ben_gear/skill/     技能定义、加载器、内置技能
include/ben_gear/mcp/       MCP 客户端、管理器、配置
include/ben_gear/net/       Socket、事件循环、HTTP 客户端
include/ben_gear/log/       异步日志前端/后端
include/ben_gear/base/      高性能基础组件
├── memory/                 内存池、分配器
├── concurrency/            线程池
├── container/              高性能字符串、对象池
├── io/                     缓冲区、文件操作
├── platform/               平台抽象（OS、CPU、线程）
└── utils/                  JSON、字符串工具
src/                        实现文件
tests/                      单元测试
examples/                   示例程序
benchmarks/                 性能测试
third_party/                第三方依赖
```

## 开发路线

- [x] 基础 Agent 框架
- [x] OpenAI 协议支持
- [x] Anthropic 协议支持
- [x] 原生工具调用 API
- [x] 会话记忆
- [x] HTTP 连接池
- [x] 技能系统（SKILL.md 渐进式披露）
- [x] MCP 协议客户端
- [ ] 流式工具调用
- [ ] 多 Agent 协作
- [ ] Web UI
- [ ] 插件系统

## 贡献

欢迎贡献！请确保：

1. 代码风格一致
2. 添加单元测试
3. 更新文档
4. 通过所有测试

## 许可证

MIT License

## 致谢

感谢以下开源项目：

- [nlohmann/json](https://github.com/nlohmann/json) - JSON 库
- OpenSSL - TLS 加密
