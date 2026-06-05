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

## MCP 工具

MCP（Model Context Protocol）工具由连接的 MCP 服务器自动发现，工具名以 `mcp_` 前缀标识。

例如，如果 MCP 服务器提供 `search` 工具，则在 BenGear 中可通过 `mcp_search` 调用。

## 工具过滤

角色机制可通过白名单过滤工具：

- `lead` 角色：可访问所有工具
- `teammate` 角色：只能访问白名单中的工具

详见 [角色机制](role.md)。

## 相关文档

- [工具架构](tools.md) - 工具系统设计
- [MCP 协议](mcp.md) - MCP 工具集成
- [技能系统](skills.md) - 技能工具详解
