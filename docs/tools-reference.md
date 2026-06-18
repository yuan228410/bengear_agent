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

## Patch / Diff 工具

代码修改优先使用 Patch 工具，而不是直接覆盖文件。Patch 工具位于后端核心层，不绑定 CLI 或 Web UI，返回结构化 JSON，便于后续 Diff Viewer、审批和回滚复用。

| 工具 | 说明 |
|------|------|
| `preview_diff` | 解析 unified diff 并预览文件变更，不写文件，计划模式可用 |
| `apply_patch` | 应用 unified diff，记录 `change_id`，用于后续回滚 |
| `list_changes` | 列出当前会话已记录的 patch 变更，计划模式可用 |
| `read_change` | 读取单个 `change_id` 的完整变更记录，计划模式可用 |
| `revert_patch` | 按 `change_id` 回滚已应用 patch |

### preview_diff

**参数：**
- `unified_diff` (string, 必需): unified diff 文本

**返回：**
- `success`: 是否解析成功
- `can_apply`: 是否可尝试应用
- `files`: 文件变更列表
- `summary`: 变更统计

### apply_patch

**参数：**
- `unified_diff` (string, 必需): unified diff 文本
- `description` (string, 可选): 本次变更说明

**返回：**
- `success`: 是否成功
- `change_id`: 变更记录 ID
- `files`: 已修改文件及 hash 信息

### list_changes

无参数。返回当前会话的 patch 变更摘要列表，包括 `change_id`、说明、创建时间、是否已回滚和文件数量。

### read_change

**参数：**
- `change_id` (string, 必需): `apply_patch` 返回的变更 ID

返回完整变更记录，包括文件路径、变更类型、before/after hash 和回滚所需元数据。

### revert_patch

**参数：**
- `change_id` (string, 必需): `apply_patch` 返回的变更 ID
- `force` (boolean, 可选): 文件已被后续修改时是否强制回滚

## Git 工具

Git 工具提供一等、结构化的只读状态能力和受权限控制的仓库写操作，避免模型通过 shell 盲调 git。

| 工具 | 说明 |
|------|------|
| `git_status` | 返回当前 workspace 的结构化 git status，计划模式可用 |
| `git_diff` | 返回 workspace 或单文件 diff，计划模式可用 |
| `git_log` | 返回结构化提交历史，计划模式可用 |
| `git_branch` | 列出、创建、切换或删除分支；写操作受权限控制 |
| `git_commit` | 创建 commit，属于写操作 |
| `git_restore` | 按路径恢复 tracked 文件修改，属于写操作 |
| `git_worktree` | 列出、创建、移除或清理 worktree；写操作受权限控制 |

### git_status

无参数。返回 branch、clean 状态和文件列表。

### git_diff

**参数：**
- `path` (string, 可选): 限定路径
- `staged` (boolean, 可选): 是否查看 staged diff
- `stat` (boolean, 可选): 是否返回 diff stat

### git_log

**参数：**
- `limit` (integer, 可选): 最大返回提交数，默认 20，上限 200
- `path` (string, 可选): 限定路径的提交历史

返回 `hash`、`short_hash`、`author`、`date`、`subject` 组成的提交列表。

### git_branch

**参数：**
- `action` (string, 可选): `list`（默认）、`create`、`switch`、`delete`
- `name` (string, 可选): 分支名，`create` / `switch` / `delete` 时使用
- `start_point` (string, 可选): 创建分支的起点
- `force` (boolean, 可选): 对支持的动作启用强制操作

`action=list` 为只读；其他 action 会经过权限策略确认。

### git_commit

**参数：**
- `message` (string, 必需): commit message
- `paths` (array, 可选): commit 前先 stage 的路径列表
- `all` (boolean, 可选): 使用 `--all` stage tracked 修改
- `amend` (boolean, 可选): amend 上一个 commit

### git_restore

**参数：**
- `paths` (array, 必需): 要恢复的路径列表，必须非空
- `staged` (boolean, 可选): 是否恢复 staged 区域
- `worktree` (boolean, 可选): 是否恢复工作区，默认 true

### git_worktree

**参数：**
- `action` (string, 可选): `list`（默认）、`add`、`remove`、`prune`
- `location` (string, 可选): add/remove 使用的相对 worktree 位置
- `branch` (string, 可选): add 使用的分支名或 ref
- `create_branch` (boolean, 可选): add 时通过 `-b` 创建新分支
- `force` (boolean, 可选): 对支持的动作启用强制操作

`action=list` 为只读；`add` / `remove` / `prune` 会经过权限策略确认。

## Test Loop 工具

Test Loop 工具用于形成“识别测试命令 → 运行测试 → 提取失败摘要 → 后续修复”的工程验证闭环。核心服务不绑定 CLI/Web UI，返回结构化 JSON，后续可与 TODO、checkpoint 和自动修复循环联动。

| 工具 | 说明 |
|------|------|
| `inspect_test_commands` | 检测当前项目并推荐可能的构建/测试命令，计划模式可用 |
| `run_tests` | 在 workspace 内运行测试/构建命令，返回 exit code、耗时、输出和失败摘要 |

### inspect_test_commands

无参数。根据项目文件识别 CMake、npm/pnpm/yarn、Cargo、Go、pytest 等常见测试入口，并按置信度排序返回建议命令。

### run_tests

**参数：**
- `command` (string, 必需): 要执行的构建或测试命令
- `cwd` (string, 可选): workspace 内工作目录，默认项目根目录
- `timeout_seconds` (integer, 可选): 超时时间，默认 120 秒，上限 3600 秒
- `max_output_bytes` (integer, 可选): 输出截断上限，默认 60000 字节

返回：
- `success`: 命令是否成功且未超时
- `timed_out`: 是否超时
- `exit_code`: 退出码
- `elapsed_ms`: 耗时
- `output`: stdout/stderr 合并输出
- `failure_summary`: 从输出中提取的失败/错误关键行

## Checkpoint / Undo 工具

Checkpoint 工具用于在修改前记录文件快照，提供会话级撤销能力。核心服务不绑定 CLI/Web UI，返回结构化 JSON，后续可与 patch/change id、测试失败回滚和 Web Diff 面板联动。

| 工具 | 说明 |
|------|------|
| `create_checkpoint` | 为指定文件创建 checkpoint，属于受权限控制的内容读取操作 |
| `list_checkpoints` | 列出当前会话 checkpoint，计划模式可用 |
| `read_checkpoint` | 读取 checkpoint 详情，计划模式可用 |
| `restore_checkpoint` | 从 checkpoint 恢复文件，属于写操作 |
| `delete_checkpoint` | 删除 checkpoint 记录，属于写操作 |

### create_checkpoint

**参数：**
- `paths` (array, 必需): 要快照的相对路径，必须非空
- `description` (string, 可选): checkpoint 说明

返回 `checkpoint_id`、创建时间、文件列表、hash 和快照内容。

### list_checkpoints

无参数。返回当前会话 checkpoint 摘要列表。

### read_checkpoint

**参数：**
- `checkpoint_id` (string, 必需): `create_checkpoint` 返回的 ID

### restore_checkpoint

**参数：**
- `checkpoint_id` (string, 必需): 要恢复的 checkpoint ID
- `paths` (array, 可选): 只恢复部分路径；不传则恢复 checkpoint 中所有文件
- `force` (boolean, 可选): 文件已变化时是否强制恢复

默认会检测已存在文件的 hash，避免静默覆盖 checkpoint 后的修改；如需覆盖需传 `force=true`。

### delete_checkpoint

**参数：**
- `checkpoint_id` (string, 必需): 要删除的 checkpoint ID

## 权限策略

工具执行前会经过基础权限策略层。当前 MVP 规则：

- 只读工具默认允许，例如 `read_file`、`preview_diff`、`list_changes`、`read_change`、`list_checkpoints`、`read_checkpoint`、`inspect_test_commands`、`git_status`、`git_diff`、`git_log`，以及 `git_branch` / `git_worktree` 的 `list` 动作。
- 写操作或命令执行默认返回结构化 `permission_required`，例如 `apply_patch`、`revert_patch`、`create_checkpoint`、`restore_checkpoint`、`delete_checkpoint`、`run_tests`、`git_restore`、`git_branch` 写动作、`git_commit`、`git_worktree` 写动作、`write_file`、`delete_file`、`execute_command`。
- workspace 外路径默认拒绝，返回 `path_outside_workspace`。
- 明显危险 shell 命令默认拒绝，返回 `shell.dangerous`。

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

## 子 Agent 工具

| 工具 | 说明 |
|------|------|
| `delegate_task` | 委派单个任务给子 Agent 执行 |
| `delegate_tasks` | 并行委派多个任务给子 Agent 执行 |

子 Agent 拥有独立的会话上下文和工具集，可以自主调用工具完成任务。子 Agent 无法再委派子 Agent（禁止递归委派）。

### delegate_task

委派单个任务给子 Agent 执行。

**参数：**
- `prompt` (string, 必需): 任务描述
- `system_prompt` (string, 可选): 覆盖系统提示，空则继承主 Agent
- `tool_filter` (array, 可选): 限制可用工具列表，空则使用全部（排除 delegate）
- `max_steps` (integer, 可选): 最大工具调用步数，默认 20
- `timeout_seconds` (integer, 可选): 超时秒数，默认 120
- `speculative_models` (array, 可选): 推测执行模型列表，多模型并行竞争取最先成功

**示例：**
```json
{
  "prompt": "查询北京天气并总结",
  "max_steps": 5,
  "timeout_seconds": 60
}
```

**返回：**
- `success` (boolean): 是否成功
- `output` (string): 子 Agent 输出（可能被截断或 LLM 摘要）
- `tool_steps` (integer): 工具调用步数
- `was_truncated` (boolean): 输出是否被截断
- `usage` (object): token 用量（prompt_tokens, completion_tokens, total_tokens）
- `artifacts` (object, 可选): 结构化产物

### delegate_tasks

并行委派多个任务给子 Agent 执行。每个子 Agent 独立运行，结果全部收集后返回。并行数受 `max_parallel` 配置限制（默认 5）。

**参数：**
- `tasks` (array, 必需): 任务数组，每项含 `prompt` 及可选参数
- `max_steps` (integer, 可选): 全局步数覆盖
- `timeout_seconds` (integer, 可选): 全局超时覆盖

**示例：**
```json
{
  "tasks": [
    {"prompt": "查询北京天气"},
    {"prompt": "查询上海天气"}
  ],
  "max_steps": 5
}
```

**返回：** 任务结果数组，每项格式同 `delegate_task` 返回值。

> **并行聚合**：当 `aggregate_parallel=true`（默认）且任务数 > 1 时，自动调用 LLM 生成聚合摘要。

MCP（Model Context Protocol）工具由连接的 MCP 服务器自动发现，工具名以 `mcp_` 前缀标识。

例如，如果 MCP 服务器提供 `search` 工具，则在 BenGear 中可通过 `mcp_search` 调用。

## 工具过滤

LLM 调用工具时，Agent 会通过 `filter_tool_calls` 过滤不安全的工具调用：

- 工具必须在 ToolRegistry 中注册
- 工具执行有超时保护（`agent.command_timeout`，默认 30 秒）
- 工作流工具使用独立超时配置
- 子 Agent 工具被自动过滤，防止递归委派

## 相关文档

- [工具架构](tools.md) - 工具系统设计
- [MCP 协议](mcp.md) - MCP 工具集成
- [技能系统](skills.md) - 技能工具详解
- [子 Agent 系统](sub_agent.md) - 子 Agent 架构和配置
