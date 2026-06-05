# MCP 协议支持

## 概述

MCP（Model Context Protocol）允许外部进程向 Agent 提供工具。BenGear 作为 MCP 客户端，连接 MCP 服务器后自动发现其工具并注册到 ToolRegistry，LLM 即可像调用内置工具一样调用 MCP 工具。

## 配置

在 `config.json` 中添加 `mcp_servers` 字段：

```json
{
  "mcp_servers": {
    "filesystem": {
      "command": "npx",
      "args": ["-y", "@modelcontextprotocol/server-filesystem", "/tmp"],
      "disabled": false
    },
    "remote-api": {
      "url": "http://localhost:8080/mcp",
      "disabled": false
    },
    "disabled-server": {
      "command": "some-server",
      "disabled": true
    }
  }
}
```

支持两种传输方式：

| 传输 | 配置字段 | 说明 |
|------|---------|------|
| stdio | `command` + `args` | 启动子进程，通过 stdin/stdout JSON-RPC 通信 |
| HTTP | `url` | 连接 HTTP 端点，通过 HTTP POST 发送 JSON-RPC 请求 |

每个服务器可配置：
- `command` — stdio 模式的可执行命令
- `args` — 命令参数数组
- `env` — 环境变量键值对
- `url` — HTTP 模式的端点地址
- `disabled` — 是否禁用（默认 false）

## 核心类型

### MCPSettings

```cpp
// include/ben_gear/config/settings.hpp

struct MCPSettings {
    int read_buffer_size = 4096;  // stdio 模式读取缓冲区大小（字节）
};
```

可在 `config.json` 中通过顶层 `mcp` 字段配置，大输出的 MCP 工具可适当调大 `read_buffer_size`。

### MCPServerConfig

```cpp
// include/ben_gear/config/settings.hpp

struct MCPServerConfig {
    container::String command;
    container::Vector<container::String> args;
    std::map<std::string, std::string> env;
    container::String url;
    bool disabled = false;
};
```

### MCPClient

```cpp
// include/ben_gear/mcp/mcp_client.hpp

class MCPClient {
public:
    /// stdio 读取超时（毫秒），0 表示无限等待
    static constexpr int default_read_timeout_ms = 30000;

    explicit MCPClient(int read_buffer_size = 4096, int read_timeout_ms = default_read_timeout_ms);

    bool connect(const MCPServerConfig& cfg);           // 连接服务器（自动选择 stdio 或 HTTP）
    container::Vector<ToolDefinition> list_tools();      // 发现工具
    std::string call_tool(const std::string& name, const Json& arguments);  // 执行工具
    void disconnect();
};
```

**stdio 读取超时**：`MCPClient` 在 stdio 模式下使用 `read_timeout_ms`（默认 30 秒）防止子进程无响应时永久阻塞。POSIX 平台通过 `poll()` 系统调用实现带超时的等待。

**安全子进程**：MCP 服务器通过 `subprocess::spawn` 启动：
- POSIX: `fork()` + `execvp()`，直接传递 argv/envp
- Windows: `CreateProcess()`
- 不经过 shell，避免命令注入

### MCPManager

```cpp
class MCPManager {
public:
    explicit MCPManager(int read_buffer_size = 4096);

    void load_servers(const std::map<std::string, MCPServerConfig>& configs);
    container::Vector<ToolDefinition> all_tool_definitions() const;
    std::string execute_tool(const std::string& name, const Json& arguments);

    /// 并行执行多个工具（同一 server 串行，不同 server 并行）
    /// 内部使用 ThreadPool，而非裸 std::thread
    std::vector<std::string> execute_tools_parallel(
        const std::vector<std::pair<std::string, Json>>& name_args_list);

    void disconnect_all();
};
```

**并行执行策略**：`execute_tools_parallel` 使用内置 `ThreadPool`（配置：min=1, max=4, queue=64），按 server 分组后提交到线程池。同一 server 的任务保持顺序串行执行，不同 server 的任务并行执行。

## 工作流程

```text
启动
  ↓
读取 mcp_servers 配置
  ↓
对每个未禁用的服务器：
  ├─ stdio 模式 → subprocess::spawn 启动子进程（fork+execvp / CreateProcess）
  └─ HTTP 模式 → 通过 HttpClient 发送 JSON-RPC over HTTP POST
  ↓
发送 initialize 请求（JSON-RPC 2.0）
  ↓
发送 tools/list 请求，获取工具定义
  ↓
将 MCP 工具注册到 ToolRegistry（mcp_ 前缀）
  ↓
LLM 调用 MCP 工具时：
  → 查找 tool_to_server_ 映射
  → 路由到对应 MCPClient
  → 发送 tools/call 请求
  ← 返回工具结果
```

## JSON-RPC 通信

BenGear 使用 JSON-RPC 2.0 协议与 MCP 服务器通信：

**初始化：**
```json
→ {"jsonrpc": "2.0", "id": 1, "method": "initialize",
   "params": {"protocolVersion": "2024-11-05", "clientInfo": {"name": "bengear", "version": "0.1.0"}}}
← {"jsonrpc": "2.0", "id": 1, "result": {"capabilities": {...}}}
→ {"jsonrpc": "2.0", "method": "notifications/initialized"}
```

**发现工具：**
```json
→ {"jsonrpc": "2.0", "id": 2, "method": "tools/list"}
← {"jsonrpc": "2.0", "id": 2, "result": {
     "tools": [{"name": "read_file", "description": "...", "inputSchema": {...}}]
   }}
```

**执行工具：**
```json
→ {"jsonrpc": "2.0", "id": 3, "method": "tools/call",
   "params": {"name": "read_file", "arguments": {"path": "/tmp/test.txt"}}}
← {"jsonrpc": "2.0", "id": 3, "result": {
     "content": [{"type": "text", "text": "file contents..."}]
   }}
```

## 工具注册

MCP 工具自动转换为 BenGear 工具格式并注册到 ToolRegistry。注册时自动添加 `mcp_` 前缀，避免与内置工具冲突：

```
MCP inputSchema → ToolParameterSchema
MCP tool name → "mcp_" + 原始名（如 search → mcp_search）
MCP tool desc → ToolDefinition.description
```

在 `SharedResources::init()` 中自动完成以下流程：
1. `MCPManager::load_servers()` 连接所有已配置的 MCP 服务器
2. 遍历 `MCPManager::all_tool_definitions()`
3. 以 `mcp_` 前缀注册到 ToolRegistry
4. executor 通过 shared_ptr 持有 MCPManager 引用，转发到 `execute_tool()`
5. 日志记录：`log::info_fmt("registered MCP tool: {} -> {}", raw_name, mcp_name)`

LLM 通过标准工具调用 API 调用 MCP 工具，无需区分内置工具和 MCP 工具。

## 安全考虑

- **安全子进程**：stdio 模式使用 fork+execvp / CreateProcess，不经过 shell，避免命令注入
- **读取超时**：stdio 模式默认 30s 超时，防止无响应子进程导致调用方永久阻塞
- **HTTP 模式**：通过 HTTP POST 通信，请求携带 `Content-Type: application/json`；服务端负责认证和授权
- `disabled: true` 可禁用特定服务器
- 建议仅连接可信的 MCP 服务器
- 服务器环境变量 `env` 可能包含敏感信息，不应提交到版本控制
