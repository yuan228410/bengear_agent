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
| HTTP | `url` | 连接 HTTP 端点（暂未实现） |

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
    explicit MCPClient(int read_buffer_size = 4096);

    bool connect(const MCPServerConfig& cfg);        // 连接服务器
    container::Vector<ToolDefinition> list_tools();   // 发现工具
    std::string call_tool(const std::string& name, const Json& arguments);  // 执行工具
    void disconnect();
};
```

### MCPManager

```cpp
class MCPManager {
public:
    explicit MCPManager(int read_buffer_size = 4096);

    void load_servers(const std::map<std::string, MCPServerConfig>& configs);
    container::Vector<ToolDefinition> all_tool_definitions() const;
    std::string execute_tool(const std::string& name, const Json& arguments);
    void disconnect_all();
};
```

## 工作流程

```text
启动
  ↓
读取 mcp_servers 配置
  ↓
对每个未禁用的服务器：
  ├─ stdio 模式 → popen 启动子进程
  └─ HTTP 模式 → 连接端点
  ↓
发送 initialize 请求（JSON-RPC 2.0）
  ↓
发送 tools/list 请求，获取工具定义
  ↓
将 MCP 工具注册到 ToolRegistry
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
MCP inputSchema  →  ToolParameterSchema
MCP tool name    →  "mcp_" + 原始名（如 search → mcp_search）
MCP tool desc    →  ToolDefinition.description
```

Agent 构造时自动完成以下流程：
1. 注册内置工具和技能工具
2. 调用 `MCPManager::load_servers()` 连接所有已配置的 MCP 服务器
3. 遍历 `MCPManager::all_tool_definitions()`，以 `mcp_` 前缀注册到 ToolRegistry
4. LLM 调用 `mcp_xxx` 工具时，executor 转发给 `MCPManager::execute_tool()` 路由到对应服务器

LLM 通过标准工具调用 API 调用 MCP 工具，无需区分内置工具和 MCP 工具。

## 安全考虑

- MCP 服务器以子进程形式运行，具有用户级权限
- `disabled: true` 可禁用特定服务器
- 建议仅连接可信的 MCP 服务器
- 服务器环境变量 `env` 可能包含敏感信息，不应提交到版本控制
