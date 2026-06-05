# 角色系统设计

## 概述

角色系统通过白名单机制控制 Agent 可使用的工具，实现不同角色的权限隔离。例如 `lead` 角色拥有全部工具权限，`teammate` 角色只能使用受限的工具集。

## 核心类型

### RoleDefinition

```cpp
// include/ben_gear/role/types.hpp

struct RoleDefinition {
    container::String name;                                // 角色名
    container::String description;                         // LLM 行为描述
    container::Vector<container::String> tool_whitelist;   // 允许的工具名列表，空=全部
    container::String tier;                                // 来源层级 "global"/"user"/"workspace"

    /// 是否允许指定工具
    bool is_tool_allowed(std::string_view tool_name) const {
        if (tool_whitelist.empty()) return true;  // 空白名单=不过滤
        for (const auto& allowed : tool_whitelist) {
            if (std::string_view(allowed.data(), allowed.size()) == tool_name) {
                return true;
            }
        }
        return false;
    }

    /// 是否不过滤（lead 角色）
    bool no_filter() const { return tool_whitelist.empty(); }
};
```

### 内置角色

| 角色 | 说明 | 白名单 |
|------|------|--------|
| `lead` | 全权限 Agent | 空（不过滤，允许所有工具） |
| `teammate` | 受限协作 Agent | `read_file`, `list_dir`, `run_command`, `http_get`, `get_skill` |

内置角色在工作空间创建时自动生成：

```json
// roles/lead.json
{"name": "lead", "description": "Full access agent", "tool_whitelist": []}

// roles/teammate.json
{"name": "teammate", "description": "Restricted agent for collaboration",
 "tool_whitelist": ["read_file", "list_dir", "run_command", "http_get", "get_skill"]}
```

## RoleLoader

### 三层级扫描

RoleLoader 按全局 → 用户 → 工作空间三层级扫描角色定义：

```cpp
// include/ben_gear/role/loader.hpp

class RoleLoader {
public:
    RoleLoader(const std::filesystem::path& global_dir,
               const std::filesystem::path& user_dir,
               const std::filesystem::path& workspace_dir);

    void discover();   // 扫描三层级目录，解析 JSON 角色文件

    std::optional<RoleDefinition> get_role(const container::String& name) const;
    const std::map<std::string, RoleDefinition>& roles() const;
};
```

### 加载规则

- 扫描每个层级的 `roles/` 目录下的 `.json` 文件
- 同名角色：后层覆盖前层（workspace > user > global）
- 角色文件的 `tier` 字段标记来源层级

## ToolFilter

### 组合模式

ToolFilter 采用组合模式，安全无侵入地过滤工具：

```cpp
// include/ben_gear/role/filter.hpp

class ToolFilter {
public:
    /// 构建过滤器：whitelist 为空表示不过滤（lead 角色）
    explicit ToolFilter(const container::Vector<container::String>& whitelist);

    /// 检查工具是否允许
    bool is_allowed(std::string_view tool_name) const;

    /// 从 ToolRegistry 生成过滤后的 OpenAI tools JSON
    Json to_openai_tools(const llm::ToolRegistry& registry) const;

    /// 从 ToolRegistry 生成过滤后的 Anthropic tools JSON
    Json to_anthropic_tools(const llm::ToolRegistry& registry) const;

    /// 从 ToolRegistry 构建仅包含白名单工具的新 Registry
    std::shared_ptr<llm::ToolRegistry> filtered_registry(
        const llm::ToolRegistry& registry) const;

    bool no_filter() const;
    size_t whitelist_size() const;
};
```

### 设计原则

ToolFilter 的设计遵循以下原则：

1. **不复制 ToolRegistryEntry**：闭包捕获裸 this 有生命周期风险
2. **不在原 registry 上 unregister**：会丢失工具
3. **维护白名单**：在序列化和执行时过滤
4. **白名单空=不过滤**：lead 角色无需特殊处理

### 过滤方式

ToolFilter 提供三种过滤方式：

| 方法 | 说明 | 使用场景 |
|------|------|----------|
| `is_allowed()` | 检查单个工具 | Agent 过滤工具调用 |
| `to_openai_tools()` / `to_anthropic_tools()` | 生成过滤后的 tools JSON | 发送给 LLM 时 |
| `filtered_registry()` | 构建新的 Registry | 需要独立 Registry 时 |

## Agent 集成

### 构造时初始化

Agent 构造时根据角色名创建 ToolFilter：

```cpp
Agent(std::shared_ptr<SharedResources> resources, container::String role = "lead")
    : resources_(std::move(resources)), ... {
    auto role_def = resources_->role_loader()->get_role(role);
    if (role_def) {
        tool_filter_ = std::make_unique<role::ToolFilter>(role_def->tool_whitelist);
    } else {
        // 角色不存在，创建不过滤的 ToolFilter
        tool_filter_ = std::make_unique<role::ToolFilter>(
            container::Vector<container::String>{});
    }
}
```

### 工具调用过滤

在工具调用循环中，Agent 过滤 LLM 返回的工具调用：

```cpp
std::vector<ToolCallRequest> Agent::filter_tool_calls(
    const std::vector<ToolCallRequest>& calls) {
    std::vector<ToolCallRequest> allowed;
    for (const auto& call : calls) {
        if (tool_filter_->is_allowed(std::string_view(call.name.data(), call.name.size()))) {
            allowed.push_back(call);
        } else {
            log::warn_fmt("tool blocked by role filter: name={}", call.name);
        }
    }
    return allowed;
}
```

### 流式路径

流式路径中同样应用过滤：

```cpp
// 在 run_session_stream_step 中
auto allowed_calls = filter_tool_calls(tool_calls);
auto tool_results = tool_manager_.execute_tools(allowed_calls);
```

## CLI 集成

```bash
# 使用 lead 角色（默认，全权限）
./bengear --role lead "hello"

# 使用 teammate 角色（受限工具集）
./bengear --role teammate "hello"

# 在 config.json 中设置默认角色
{
  "role": "teammate"
}
```

## 自定义角色

在工作空间的 `roles/` 目录创建 JSON 文件：

```json
// roles/reviewer.json
{
  "name": "reviewer",
  "description": "Read-only code reviewer agent",
  "tool_whitelist": [
    "read_file",
    "list_directory",
    "search_files",
    "grep_content",
    "get_skill",
    "read_memory",
    "recall"
  ]
}
```

```json
// roles/deployer.json
{
  "name": "deployer",
  "description": "Deployment agent with command execution",
  "tool_whitelist": [
    "read_file",
    "execute_command",
    "http_get",
    "http_post",
    "get_skill"
  ]
}
```

## 多 Agent 协作

当前架构支持通过 SharedResources 创建多个 Agent 实例，每个使用不同角色：

```cpp
auto resources = std::make_shared<SharedResources>(settings, ws_ctx);

Agent lead_agent(resources, "lead");       // 全权限
Agent teammate_agent(resources, "teammate"); // 受限权限
Agent reviewer_agent(resources, "reviewer"); // 只读权限

// 共享 ToolRegistry、MemoryStore 等资源
// 各自独立过滤工具调用
```

## 最佳实践

1. **最小权限原则**：为每个角色分配最少必要工具
2. **角色命名清晰**：使用描述性名称（reviewer、deployer、observer）
3. **白名单设计**：只允许安全操作，避免 `write_file`、`delete_file`、`execute_command` 除非必要
4. **角色描述**：提供清晰的 LLM 行为描述，影响 Agent 的自我认知
5. **层级覆盖**：工作空间级角色覆盖用户/全局级同名角色
6. **日志审计**：被过滤的工具调用会记录 `log::warn_fmt`
