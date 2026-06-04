# 工具系统设计

## 目标

工具系统为 LLM 提供受控的本地能力访问，同时保持工具执行与提供商协议独立。

设计目标：

- **高内聚**：每个工具专注单一能力
- **低耦合**：工具不依赖提供商客户端
- **可扩展**：添加工具应局部化
- **可观测**：工具调用和结果发出回调
- **类型安全**：使用 JSON Schema 定义参数

## 核心架构

### 1. 工具类型定义 (`tool_types.hpp`)

```cpp
// 工具参数 Schema
struct ToolParameterSchema {
    container::String type;        // "string", "number", "boolean", "object", "array"
    container::String description;
    std::optional<Json> properties;
    std::optional<container::Vector<container::String>> required;
    std::optional<Json> items;
    std::optional<Json> enum_values;
};

// 工具定义
struct ToolDefinition {
    container::String name;
    container::String description;
    container::Vector<std::pair<container::String, ToolParameterSchema>> parameters;

    Json to_openai_format() const;    // 转换为 OpenAI 格式
    Json to_anthropic_format() const;  // 转换为 Anthropic 格式
};

// 工具调用请求
struct ToolCallRequest {
    container::String id;          // 调用 ID
    container::String name;        // 工具名称
    Json arguments;                // 参数（JSON 对象）

    static ToolCallRequest from_openai(const Json& j);
    static ToolCallRequest from_anthropic(const Json& j);
};

// 工具执行结果
struct ToolResult {
    bool success;
    container::String output;
    container::String error;

    static ToolResult ok(container::String output);
    static ToolResult not_found(std::string_view name);
    static ToolResult execution_error(std::string_view name, std::string_view what);
    static ToolResult unknown_error(std::string_view name);
};

// 工具调用结果（LLM 协议层）
struct ToolCallResult {
    container::String tool_call_id;
    container::String name;
    container::String output;
    bool success;
};
```

### 2. 工具注册表 (`tool_registry.hpp`)

```cpp
class ToolRegistry {
public:
    // 注册工具
    void register_tool(
        const container::String& name,
        const container::String& description,
        const container::Vector<std::pair<container::String, ToolParameterSchema>>& parameters,
        ToolExecutor executor
    );

    // 查找工具
    const ToolRegistryEntry* find(const std::string& name) const;

    // 检查工具是否存在
    bool has_tool(const std::string& name) const;

    // 注销工具
    bool unregister_tool(const std::string& name);

    // 执行工具
    ToolResult execute(const std::string& name, const Json& arguments) const;

    // 转换为协议格式
    Json to_openai_tools() const;
    Json to_anthropic_tools() const;
};
```

### 3. 工具调用管理器 (`tool_call_manager.hpp`)

```cpp
class ToolCallManager {
public:
    explicit ToolCallManager(const ToolRegistry& registry);
    
    // 从响应中提取工具调用
    std::vector<ToolCallRequest> extract_openai_tool_calls(const Json& response) const;
    std::vector<ToolCallRequest> extract_anthropic_tool_calls(const Json& response) const;
    
    // 执行工具
    ToolCallResult execute_tool(const ToolCallRequest& request) const;
    std::vector<ToolCallResult> execute_tools(const std::vector<ToolCallRequest>& requests) const;
    
    // 构建工具结果消息
    Json build_openai_tool_results(const std::vector<ToolCallResult>& results) const;
    Json build_anthropic_tool_results(const std::vector<ToolCallResult>& results) const;
    
    // 检查是否有工具调用
    static bool has_tool_calls(const Json& response, Provider provider);
};
```

## 内置工具

### 文件工具

| 工具 | 说明 | 参数 |
|------|------|------|
| `read_file` | 读取文件内容，支持行范围 | `path`, `start_line?`, `end_line?` |
| `write_file` | 写入文件，自动创建父目录 | `path`, `content`, `mode?`("overwrite"/"append") |
| `delete_file` | 删除文件或目录 | `path`, `recursive?`(bool) |
| `list_directory` | 列出目录内容 | `path` |
| `rename_file` | 重命名/移动文件 | `src`, `dst` |
| `copy_file` | 复制文件或目录 | `src`, `dst`, `recursive?`(bool) |
| `mkdir` | 创建目录，默认递归创建父目录 | `path`, `parents?`(bool, 默认true) |
| `file_info` | 获取文件/目录信息 | `path` |

### 命令工具

| 工具 | 说明 | 参数 |
|------|------|------|
| `execute_command` | 执行 shell 命令，返回 stdout/stderr 合并输出 + exit code | `command`, `timeout?`, `cwd?` |

返回 JSON：`{"stdout": "...", "exit_code": 0, "success": true}`

### HTTP 工具

| 工具 | 说明 | 参数 |
|------|------|------|
| `http_get` | HTTP GET 请求 | `url`, `headers?`(array) |
| `http_post` | HTTP POST JSON 请求 | `url`, `body`, `headers?`(array) |

返回 JSON：`{"status": 200, "body": "..."}`

### 搜索工具

| 工具 | 说明 | 参数 |
|------|------|------|
| `search_files` | 按文件名模式搜索文件 | `path`, `pattern`(glob) |
| `grep_content` | 按正则搜索文件内容 | `path`, `pattern`(regex), `file_pattern?`, `max_results?` |

### 技能管理工具

| 工具 | 说明 | 参数 |
|------|------|------|
| `install_skill` | 从远程 zip/本地 zip/本地目录安装技能 | `source`, `scope?`("project"/"global") |
| `remove_skill` | 移除已安装的技能 | `name`, `scope?` |
| `enable_skill` | 启用已禁用的技能 | `name` |
| `disable_skill` | 禁用技能 | `name` |
| `list_skills` | 列出所有技能及状态 | 无 |
| `get_skill` | 按需加载技能完整内容 | `name` |

## 执行流程

```text
LLM 响应
  → ToolCallManager 提取工具调用
  → ToolRegistry 查找工具
  → 执行工具函数
  → 发出回调
  → 构建工具结果消息
  → 追加到对话历史
  → 再次调用 LLM
```

## 协议适配

### OpenAI 格式

**请求：**
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

**响应：**
```json
{
  "choices": [{
    "message": {
      "tool_calls": [{
        "id": "call_abc123",
        "type": "function",
        "function": {
          "name": "read_file",
          "arguments": "{\"path\": \"/tmp/test.txt\"}"
        }
      }]
    }
  }]
}
```

### Anthropic 格式

**请求：**
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

**响应：**
```json
{
  "content": [{
    "type": "tool_use",
    "id": "toolu_abc123",
    "name": "read_file",
    "input": {"path": "/tmp/test.txt"}
  }]
}
```

## 回调接口

```cpp
class AgentCallbacks {
public:
    virtual void on_token(std::string_view token) {}
    virtual void on_thinking(std::string_view token) {}
    virtual void on_tool_call(const ToolCallRequest& call) {}
    virtual void on_tool_result(const ToolCallResult& result) {}
};
```

## 自定义工具

### 示例：数据库查询工具

```cpp
agent.register_tool(
    "query_database",
    "执行 SQL 查询并返回结果",
    {
        {"sql", ToolParameterSchema{
            .type = "string",
            .description = "SQL 查询语句"
        }},
        {"limit", ToolParameterSchema{
            .type = "integer",
            .description = "返回行数限制（默认：100）"
        }}
    },
    [](const Json& args) -> container::String {
        std::string sql = args["sql"].get<std::string>();
        int limit = args.value("limit", 100);

        // 执行查询
        auto results = execute_sql(sql, limit);

        // 返回 JSON 格式结果
        return container::String(results.dump().c_str());
    }
);
```

### 示例：API 调用工具

```cpp
agent.register_tool(
    "call_api",
    "调用外部 API",
    {
        {"endpoint", ToolParameterSchema{
            .type = "string",
            .description = "API 端点"
        }},
        {"method", ToolParameterSchema{
            .type = "string",
            .description = "HTTP 方法",
            .enum_values = Json::array({"GET", "POST", "PUT", "DELETE"})
        }},
        {"body", ToolParameterSchema{
            .type = "object",
            .description = "请求体（JSON 对象）"
        }}
    },
    [](const Json& args) -> container::String {
        // 实现 API 调用
        return container::String(response.c_str());
    }
);
```

## 安全考虑

`write_file` 和 `run_command` 功能强大。在暴露给不受信任用户前，需添加策略层：

- ✅ 允许的目录白名单
- ✅ 命令白名单/黑名单
- ✅ 用户确认机制
- ✅ 执行超时限制
- ✅ 输出大小限制
- ✅ 审计日志记录

## 技能系统

工具提供可执行能力，技能提供 LLM 指令。两者正交互补。

详见 [skills.md](skills.md)。

### get_skill 工具

技能系统注册了一个特殊工具 `get_skill`，LLM 通过它按需加载技能内容：

```cpp
registry.register_tool(
    "get_skill",
    "Load a skill's full content by name. Use this when you need detailed instructions for a skill.",
    {{"name", ToolParameterSchema{.type = "string", .description = "Skill name to load"}}},
    [loader](const Json& args) -> container::String {
        return container::String(loader->get_skill_content(args["name"].get<std::string>()).c_str());
    }
);
```

## MCP 工具

MCP 服务器提供的工具自动注册到 ToolRegistry，注册名自动添加 `mcp_` 前缀（如原始工具 `search` 注册为 `mcp_search`），与内置工具无差别。LLM 通过标准工具调用 API 调用 MCP 工具。

详见 [mcp.md](mcp.md)。

## 扩展指南

添加新工具的步骤：

1. **定义工具**：使用 `register_tool()` 注册
2. **参数验证**：使用 JSON Schema 定义参数
3. **实现逻辑**：在执行函数中实现工具逻辑
4. **错误处理**：返回清晰的错误消息
5. **测试覆盖**：添加成功和失败路径的测试

## 最佳实践

1. **单一职责**：每个工具只做一件事
2. **清晰描述**：工具描述要准确、简洁
3. **参数验证**：检查必需参数和类型
4. **错误消息**：提供有用的错误信息
5. **幂等性**：尽可能设计幂等操作
6. **资源清理**：确保资源正确释放
7. **日志记录**：记录关键操作
8. **性能考虑**：避免阻塞操作

## 测试

```cpp
// 测试工具注册
auto registry = create_builtin_tool_registry();
require(registry.find("read_file") != nullptr);

// 测试工具执行
Json args = {{"path", "/tmp/test.txt"}};
auto result = registry.execute("read_file", args);
require(result.success);
```
