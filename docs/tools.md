# 工具系统设计

## 目标

工具系统为 LLM 提供受控的本地能力访问，同时保持工具执行与提供商协议独立。

设计目标：

- **高内聚**：每个工具专注单一能力
- **低耦合**：工具不依赖提供商客户端
- **可扩展**：添加工具应局部化
- **可观测**：工具调用和结果发出回调
- **类型安全**：使用 JSON Schema 定义参数

## 文件位置

工具系统位于 `capabilities/tool/`。原 `builtin_tools.cpp`（1240 行）已拆分为 8 个独立文件：
- `file_tools.cpp` — 文件工具（read/write/delete/list 等）
- `shell_tools.cpp` — shell 命令工具
- `http_tools.cpp` — HTTP 请求工具
- `extended_tools.cpp` — 扩展工具
- `replace_tools.cpp` — 替换工具
- `search_content_tools.cpp` — 搜索内容工具
- `env_tools.cpp` — 环境变量工具
- `image_tools.cpp` — 图片工具

## 子 Agent 工具

子 Agent 工具位于 `agent/runtime/sub_agent_tools.hpp/cpp`（`delegate_task` / `delegate_tasks`），以及通过 `~/.bengear/agents/sub/*.md` 文件动态注册的自定义子 Agent（`sub_<name>`）。

### delegate_task

委派单个独立子任务给子 Agent。子 Agent 拥有独立的 ReAct 循环（调 LLM → 执行工具 → 循环 → 返回结果）。

**适用场景**：噪声大、数据量大、但独立的脏活累活（解析日志、查 API、批量处理文件）。子 Agent 返回摘要结果，主 Agent 拿精华不拿噪音。

**参数**：
| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `prompt` | string | ✅ | 任务描述，需包含所有上下文 |
| `timeout` | integer | ❌ | 超时（毫秒） |

### delegate_tasks

并行委派多个独立子任务给多个子 Agent。每个子 Agent 独立执行 ReAct 循环。

**适用场景**：可以拆成多片并行干的独立任务（同时查多个城市的天气、同时分析多个文件）。

**参数**：
| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `prompts` | array | ✅ | 任务列表，每个元素是字符串或对象（含 `prompt`、`system_prompt`、`id`） |
| `max_parallel` | integer | ❌ | 最大并行数，默认 5 |
| `auto_summary` | boolean | ❌ | 是否自动摘要 |

### 自定义子 Agent（`sub_<name>`）

见 [子 Agent 系统](sub_agent.md) 文档。

## 核心架构

### 1. 工具类型定义 (`tool_types.hpp`)

```cpp
// 工具参数 Schema
struct ToolParameterSchema {
    std::string type;          // "string", "number", "boolean", "object", "array", "integer"
    std::string description;
    std::optional<Json> properties;
    std::optional<std::vector<std::string>> required;
    std::optional<Json> items;
    std::optional<Json> enum_values;
};

// 工具定义
struct ToolDefinition {
    std::string name;
    std::string description;
    std::vector<std::pair<std::string, ToolParameterSchema>> parameters;

    Json to_openai_format() const;
    Json to_anthropic_format() const;
};

// 工具调用请求（定义于 acp/types/tool_call_types.hpp）
struct ToolCallRequest {
    std::string id;            // 调用 ID
    std::string name;          // 工具名称
    Json arguments;                  // 参数（JSON 对象）

    static ToolCallRequest from_openai(const Json& j);
    static ToolCallRequest from_anthropic(const Json& j);
};

// 工具执行结果（定义于 acp/types/tool_call_types.hpp）
struct ToolCallResult {
    std::string tool_call_id;
    std::string name;
    std::string output;
    bool success;
};
```

### 2. 工具注册表 (`tool_registry.hpp`)

```cpp
class ToolRegistry {
public:
    void register_tool(
        const std::string& name,
        const std::string& description,
        const std::vector<std::pair<std::string, ToolParameterSchema>>& parameters,
        ToolExecutor executor
    );

    std::optional<ToolRegistryEntry> find(const std::string& name) const;
    bool has_tool(const std::string& name) const;
    bool unregister_tool(const std::string& name);
    ToolResult execute(const std::string& name, const Json& arguments) const;

    Json to_openai_tools() const;
    Json to_anthropic_tools() const;

    void for_each(std::function<void(std::string_view, const ToolRegistryEntry&)> fn) const;
};
```

线程安全：内部使用 `shared_mutex`，`register_tool` 和 `unregister_tool` 使用独占锁，`find`/`execute`/`for_each` 使用共享锁。

### 3. 工具调用管理器 (`tool_call_manager.hpp`)

```cpp
class ToolCallManager {
public:
    explicit ToolCallManager(const ToolRegistry& registry, std::chrono::seconds timeout);

    std::vector<ToolCallRequest> extract_openai_tool_calls(const Json& response) const;
    std::vector<ToolCallRequest> extract_anthropic_tool_calls(const Json& response) const;

    ToolCallResult execute_tool(const ToolCallRequest& request) const;
    std::vector<ToolCallResult> execute_tools(const std::vector<ToolCallRequest>& requests) const;

    Json build_openai_tool_results(const std::vector<ToolCallResult>& results) const;
    Json build_anthropic_tool_results(const std::vector<ToolCallResult>& results) const;

    static bool has_tool_calls(const Json& response, Provider provider);
};
```

## 工具注册

工具在 `RuntimeFactory` 初始化工具系统时统一注册（原 `Runtime::init_tools()` 已移至 `RuntimeFactory`）：

```cpp
void RuntimeFactory::init_tool_system(Runtime& runtime) {
    auto& tools = *runtime.services().resolve<capabilities::tool::ToolRegistry>();
    auto& settings = *runtime.services().resolve<config::Settings>();
    auto& skill_loader = ...;
    
    tools::register_all_tools(tools, settings.agent.command_timeout, &skill_loader);
    // 记忆工具、工作空间工具等在对应模块的 tool_registration.cpp 中注册
    // 工作流工具在 RuntimeFactory::init_orchestration() 中注册
    // MCP 工具在 MCP 连接后注册...
}
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
| `search_files` | 按 glob 模式搜索文件 | `path`, `pattern` |
| `grep_content` | 按正则搜索文件内容 | `path`, `pattern`, `file_pattern?`, `max_results?` |

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

### 记忆工具

| 工具 | 说明 | 参数 |
|------|------|------|
| `read_memory` | 读取长期记忆，支持指定层级 | `tier?`("global"/"user"/"workspace") |
| `write_memory` | 写入长期记忆到指定层级 | `content`, `tier?`("user"/"workspace") |
| `recall` | Section 级别关键词搜索 | `keyword`, `section_only?`(bool) |
| `read_soul` | 读取身份定义 | 无 |
| `write_soul` | 写入身份定义 | `content`, `tier?` |
| `read_rules` | 读取行为规范 | 无 |
| `write_rules` | 写入行为规范 | `content`, `tier?` |
| `append_episode` | 追加到今日情景记忆 | `content` |
| `read_episode` | 读取今日情景记忆 | 无 |
| `read_episode_range` | 按日期范围读取情景记忆 | `from`, `to?` |

记忆工具注册函数：`register_memory_tools(ToolRegistry&, shared_ptr<MemoryStore>)`

> 注意：`register_memory_tools` 只注册长期记忆工具（`read_memory`/`write_memory`/`recall`/`read_soul`/`write_soul`/`read_rules`/`write_rules`/`read_user`/`write_user`），**不**接收 `EpisodeStore` 或路径参数。情景记忆工具（`append_episode`/`read_episode`/`read_episode_range`）由 `register_episode_tools(ToolRegistry&, shared_ptr<EpisodeStore>)` 单独注册，在 Session 构造后调用（因为依赖 Session 的 EpisodeStore）。EpisodeStore 不再使用文件系统目录，而是通过 `SessionDeps::history_db`（`HistoryDB*`）持久化。

### 工作空间工具

| 工具 | 说明 | 参数 |
|------|------|------|
| `list_workspaces` | 列出所有工作空间 | 无 |
| `create_workspace` | 创建新工作空间 | `name`, `project_path?` |
| `remove_workspace` | 软删除工作空间 | `name` |
| `restore_workspace` | 恢复已删除的工作空间 | `name` |

工作空间工具注册函数：`register_workspace_tools(ToolRegistry&, shared_ptr<WorkspaceManager>)`

### 技能工具

| 工具 | 说明 | 参数 |
|------|------|------|
| `get_skill` | 按需加载技能完整内容（Level 2） | `name` |
| `install_skill` | 从远程/本地安装技能 | `source`, `scope?`("project"/"global") |
| `remove_skill` | 移除技能 | `name`, `scope?` |
| `enable_skill` | 启用技能 | `name` |
| `disable_skill` | 禁用技能 | `name` |
| `list_skills` | 返回所有技能的 JSON 列表 | 无 |

详见 [skills.md](skills.md)。

### MCP 工具

MCP 服务器提供的工具自动注册到 ToolRegistry，注册名自动添加 `mcp_` 前缀（如原始工具 `search` 注册为 `mcp_search`），与内置工具无差别。LLM 通过标准工具调用 API 调用 MCP 工具。

详见 [mcp.md](mcp.md)。

## 执行流程

```text
LLM 响应
  → ToolCallManager 提取工具调用
  → ToolRegistry 查找工具
  → 执行工具函数
  → 发出回调
  → 构建工具结果消息
  → 追加到对话历史
  → 持久化到 HistoryDB
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
    virtual void on_token(std::string_view token) const {}
    virtual void on_thinking(std::string_view token) const {}
    virtual void on_tool_call(const acp::ToolCallRequest& call) const {}
    virtual void on_tool_result(const acp::ToolCallResult& result) const {}
};
```


## 自定义工具

### 示例：数据库查询工具

```cpp
auto* registry = runtime->services().resolve<capabilities::tool::ToolRegistry>();
registry->register_tool(
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
    [](const Json& args) -> std::string {
        std::string sql = args["sql"].get<std::string>();
        int limit = args.value("limit", 100);
        auto results = execute_sql(sql, limit);
        return std::string(results.dump().c_str());
    }
);
```

## 安全考虑

`write_file` 和 `execute_command` 功能强大。在暴露给不受信任用户前，需添加策略层：

- ✅ 允许的目录白名单
- ✅ 命令白名单/黑名单
- ✅ 用户确认机制
- ✅ 执行超时限制（`command_timeout`）
- ✅ 输出大小限制
- ✅ 审计日志记录

## 扩展指南

添加新工具的步骤：

1. **定义工具**：使用 `register_tool()` 注册
2. **参数验证**：使用 JSON Schema 定义参数
3. **实现逻辑**：在执行函数中实现工具逻辑
4. **错误处理**：返回清晰的错误消息
5. **注册入口**：在 `register_all_tools` 或独立注册函数中调用
6. **测试覆盖**：添加成功和失败路径的测试

## 最佳实践

1. **单一职责**：每个工具只做一件事
2. **清晰描述**：工具描述要准确、简洁
3. **参数验证**：检查必需参数和类型
4. **错误消息**：提供有用的错误信息
5. **幂等性**：尽可能设计幂等操作
6. **资源清理**：确保资源正确释放
7. **日志记录**：记录关键操作（`log::debug_fmt`/`log::error_fmt`）
8. **性能考虑**：避免阻塞操作
