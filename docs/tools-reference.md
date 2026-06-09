# 工具参考

BenGear 提供了丰富的内置工具，分为以下几类：

## 文件工具

| 工具 | 说明 |
|------|------|
| `read_file` | 读取文件内容，支持行范围筛选 |
| `write_file` | 写入文件，自动创建父目录，支持 append/overwrite |
| `delete_file` | 删除文件或目录，支持递归删除 |
| `list_directory` | 列出目录内容，支持递归 |
| `rename_file` | 重命名或移动文件/目录 |
| `copy_file` | 复制文件 |
| `mkdir` | 创建目录，默认递归创建父目录 |
| `file_info` | 获取文件/目录信息（大小、修改时间等） |
| `search_files` | 按 glob 模式搜索文件 |
| `grep_content` | 按正则搜索文件内容，返回行号和匹配 |

### read_file

读取文件内容，支持行号范围筛选。

**参数：**
- `path` (string, 必需): 文件路径
- `start_line` (integer, 可选): 起始行号（从 1 开始）
- `end_line` (integer, 可选): 结束行号（含）

**示例：**
```json
{
  "path": "/path/to/file.txt",
  "start_line": 10,
  "end_line": 20
}
```

### write_file

写入文件，自动创建父目录。

**参数：**
- `path` (string, 必需): 文件路径
- `content` (string, 必需): 要写入的内容
- `mode` (string, 可选): 写入模式，`overwrite`（默认）或 `append`

**示例：**
```json
{
  "path": "/path/to/file.txt",
  "content": "Hello, World!",
  "mode": "overwrite"
}
```

### delete_file

删除文件或目录。

**参数：**
- `path` (string, 必需): 文件或目录路径
- `recursive` (boolean, 可选): 是否递归删除目录，默认 false

**示例：**
```json
{
  "path": "/path/to/directory",
  "recursive": true
}
```

### list_directory

列出目录内容。

**参数：**
- `path` (string, 可选): 目录路径，默认当前目录
- `recursive` (boolean, 可选): 是否递归列出子目录，默认 false
- `include` (string, 可选): 文件名 glob 过滤，如 `*.py`
- `recursive` (boolean, 可选): 是否递归列出子目录，默认 false
- `include` (string, 可选): 文件名 glob 过滤，如 `*.py`

**示例：**
```json
{
  "path": "/path/to/directory",
  "recursive": true,
  "include": "*.cpp"
}
```

### search_files

按 glob 模式搜索文件。

**参数：**
- `pattern` (string, 必需): 搜索模式（支持正则）
- `path` (string, 必需): 搜索目录
- `include` (string, 可选): 文件名过滤，如 `*.py`
- `max_results` (integer, 可选): 最大返回条数，默认 50

**示例：**
```json
{
  "pattern": "TODO|FIXME",
  "path": "/path/to/project",
  "include": "*.cpp",
  "max_results": 100
}
```

## Shell 工具

### execute_command

执行 shell 命令。

**参数：**
- `command` (string, 必需): 要执行的 shell 命令
- `cwd` (string, 可选): 工作目录
- `timeout` (integer, 可选): 超时秒数，默认 30

**示例：**
```json
{
  "command": "ls -la",
  "cwd": "/path/to/project",
  "timeout": 60
}
```

**返回：**
- `stdout`: 标准输出
- `stderr`: 标准错误
- `exit_code`: 退出码

## HTTP 工具

### http_get

HTTP GET 请求。

**参数：**
- `url` (string, 必需): 请求 URL
- `headers` (object, 可选): 请求头

**示例：**
```json
{
  "url": "https://api.example.com/data",
  "headers": {
    "Authorization": "Bearer token"
  }
}
```

### http_post

HTTP POST JSON 请求。

**参数：**
- `url` (string, 必需): 请求 URL
- `body` (object, 必需): 请求体（JSON）
- `headers` (object, 可选): 请求头

**示例：**
```json
{
  "url": "https://api.example.com/data",
  "body": {
    "key": "value"
  },
  "headers": {
    "Content-Type": "application/json"
  }
}
```

## 记忆工具

| 工具 | 说明 |
|------|------|
| `read_memory` | 读取长期记忆，支持指定层级或合并 |
| `write_memory` | 写入长期记忆到指定层级 |
| `recall` | Section 级别关键词搜索 |
| `read_soul` | 读取身份定义 |
| `write_soul` | 写入身份定义 |
| `read_rules` | 读取行为规范 |
| `write_rules` | 写入行为规范 |
| `append_episode` | 追加到今日情景记忆 |

### read_memory

读取长期记忆。

**参数：**
- `keyword` (string, 可选): 关键词过滤
- `level` (string, 可选): 层级过滤，`global`/`user`/`workspace`

**示例：**
```json
{
  "keyword": "project",
  "level": "user"
}
```

### write_memory

写入长期记忆。

**参数：**
- `content` (string, 必需): 要记住的内容
- `category` (string, 可选): 记忆分类
- `level` (string, 可选): 层级，默认 `user`

**示例：**
```json
{
  "content": "项目使用 C++20 构建",
  "category": "project_info",
  "level": "workspace"
}
```

## 工作空间工具

| 工具 | 说明 |
|------|------|
| `list_workspaces` | 列出所有工作空间 |
| `create_workspace` | 创建新工作空间 |
| `remove_workspace` | 软删除工作空间 |
| `restore_workspace` | 恢复已删除的工作空间 |

### create_workspace

创建新工作空间。

**参数：**
- `name` (string, 必需): 工作空间名称
- `project_path` (string, 可选): 项目路径

**示例：**
```json
{
  "name": "my-project",
  "project_path": "/path/to/project"
}
```

## 技能工具

| 工具 | 说明 |
|------|------|
| `get_skill` | 按需加载技能完整内容（Level 2） |
| `install_skill` | 从远程/本地安装技能 |
| `remove_skill` | 移除技能 |
| `enable_skill` | 启用技能 |
| `disable_skill` | 禁用技能 |
| `list_skills` | 返回所有技能的 JSON 列表 |

### install_skill

安装技能。

**参数：**
- `name` (string, 必需): 技能名称
- `source` (string, 可选): 压缩包地址（URL 或本地路径）
- `content` (string, 可选): 技能内容（Markdown）
- `level` (string, 可选): 安装层级，默认 `user`

**示例：**
```json
{
  "name": "frontend-design",
  "source": "https://example.com/skill.tar.gz",
  "level": "workspace"
}
```

## 工作流工具

| 工具 | 说明 | 超时 |
|------|------|------|
| `create_workflow` | 创建多任务工作流 | 60s |
| `execute_workflow` | 执行已创建的工作流 | 300s |
| `list_workflow_templates` | 列出可用工作流模板 | 30s |
| `get_workflow_status` | 获取工作流执行状态 | 30s |
| `cancel_workflow` | 取消正在执行的工作流 | 30s |
| `list_workflows` | 列出所有工作流 | 30s |
| `visualize_workflow` | 生成 Mermaid/DOT 可视化 | 30s |
| `export_workflow` / `import_workflow` | 导入导出工作流定义 | 30s |

> **超时说明**：工作流工具使用独立的超时配置（`ToolCallManager::set_tool_timeout`），不受 `command_timeout` 默认 30s 限制。

### create_workflow

创建包含多任务的工作流，任务可并行或串行执行。

**参数：**
- `name` (string, 必需): 工作流名称
- `tasks` (array, 必需): 任务列表，每项包含 `id`、`type`（llm/tool/function）、`prompt`、可选 `depends_on` 和 `config`
- `variables` (object, 可选): 全局变量
- `on_failure` (string, 可选): 失败策略，`abort`（默认）/`continue`/`rollback`

**示例：**
```json
{
  "name": "weather-compare",
  "tasks": [
    {"id": "fetch-shanghai", "type": "tool", "config": {"tool": "http_get", "params": {"url": "https://wttr.in/Shanghai?format=j1"}}},
    {"id": "fetch-beijing", "type": "tool", "config": {"tool": "http_get", "params": {"url": "https://wttr.in/Beijing?format=j1"}}},
    {"id": "compare", "type": "llm", "prompt": "对比上海{{fetch-shanghai}}和北京{{fetch-beijing}}天气", "depends_on": ["fetch-shanghai", "fetch-beijing"]}
  ]
}
```

### execute_workflow

执行已注册的工作流。

**参数：**
- `workflow_id` (string, 必需): 工作流 ID（由 `create_workflow` 返回）

---

## MCP 工具

MCP（Model Context Protocol）工具由连接的 MCP 服务器自动发现，工具名以 `mcp_` 前缀标识。

例如，如果 MCP 服务器提供 `search` 工具，则在 BenGear 中可通过 `mcp_search` 调用。

## 工具过滤

LLM 调用工具时，Agent 会通过 `filter_tool_calls` 过滤不安全的工具调用：

- 工具必须在 ToolRegistry 中注册
- 工具执行有超时保护（`agent.command_timeout`，默认 30 秒）
- 工作流工具使用独立超时配置

## 相关文档

- [工具架构](tools.md) - 工具系统设计
- [MCP 协议](mcp.md) - MCP 工具集成
- [技能系统](skills.md) - 技能工具详解
