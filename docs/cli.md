# CLI 参考

## 全局选项

```text
-h, --help                      显示帮助
-c, --config <path>             JSON 模型配置路径
--active-model <name>           模型键名（格式：provider:model）
--provider <name>               提供商：openai 或 anthropic
-m, --model <name>              模型名称
--base-url <url>                API 基础 URL
--api-url <url>                 完整 API 端点 URL
--api-key <key>                 API 密钥
--llm-request-retry-attempts <n> 重试次数
--no-stream                      禁用流式响应（默认流式）
--stdin                         从 stdin 读取提示
--show-config                   显示解析后的配置
--list-skills                   列出所有可用技能
--md-raw                        禁用 Markdown 渲染（显示原始文本）
--no-banner                     禁用启动 banner
--no-thinking                   隐藏 thinking 输出
--no-tool                       隐藏工具调用输出
--no-detail                     同时隐藏 thinking 和工具调用输出
--user <name>                   设置用户名（默认：default）
--workspace-name <name>         设置工作空间名（默认：default）
--workspace <path>              设置项目工作空间路径
--session <id>                  恢复指定会话
--new-session                   强制创建新会话
--async / --sync                启用异步/同步模式
```

## 子命令

### workspace - 工作空间管理

```bash
# 列出所有工作空间
./build/bengear workspace list

# 创建新工作空间
./build/bengear workspace create <name> --project-path <path>

# 软删除工作空间
./build/bengear workspace remove <name>

# 恢复已删除的工作空间
./build/bengear workspace restore <name>
```

### session - 会话管理

```bash
# 列出所有会话
./build/bengear session list

# 删除指定会话
./build/bengear session delete <session_id>
```

### serve - HTTP/WebSocket Server

```bash
./build/bengear serve
```

启动 Server 模式，监听 `config.server.host:config.server.port`（默认 `0.0.0.0:8080`）。当前命令没有独立的 `--port`/`--host` 参数；如需调整监听地址，需先让配置加载器支持 `server` 字段或在代码默认值中修改。

Server 当前提供 WebSocket、静态文件、会话/配置/工作空间/MCP/文件 REST API；OpenAI 兼容 `/v1/chat/completions` 仍是开发中能力。

## 交互式 REPL

不带参数直接进入交互式聊天模式。REPL 提供完整的行编辑、历史浏览和命令补全功能。

### 行编辑快捷键

| 快捷键 | 功能 |
|--------|------|
| `←` / `Ctrl+B` | 光标左移 |
| `→` / `Ctrl+F` | 光标右移 |
| `Home` / `Ctrl+A` | 光标移到行首 |
| `End` / `Ctrl+E` | 光标移到行尾 |
| `Backspace` | 删除光标前字符 |
| `Delete` | 删除光标处字符 |
| `Ctrl+U` | 删除光标前全部内容 |
| `Ctrl+K` | 删除光标后全部内容 |
| `Ctrl+W` | 删除光标前一个单词 |
| `Ctrl+L` | 清屏 |
| `↑` | 上一条历史记录 |
| `↓` | 下一条历史记录 |
| `Ctrl+C` | 首次按显示提示，连按两次退出 |
| `Ctrl+D` | 空行时退出 |

### `/` 命令

在 REPL 中输入 `/` 自动显示可用命令列表，使用 Tab/Shift+Tab 在候选间切换，Space/Enter 确认选择。

| 命令 | 说明 | 参数 |
|------|------|------|
| `/help` | 显示帮助信息 | - |
| `/exit` `/quit` | 退出 REPL | - |
| `/new` | 创建新会话 | - |
| `/sessions` | 列出历史会话 | - |
| `/resume <id>` | 恢复历史会话 | session_id（Tab 补全） |
| `/plan` | 进入计划模式（read-only 探索） | Tab 补全 |
| `/plan off` | 退出计划模式 | - |
| `/cancel` | 取消当前请求 | - |
| `/compact` | 手动上下文压缩 | - |
| `/clear` | 清屏 | - |
| `/history [n]` | 显示最近 n 条历史消息（默认 20） | 数字 |
| `/search <kw>` | 搜索历史消息（FTS5 全文检索） | 关键词 |
| `/export [file]` | 导出当前会话为 Markdown | 文件名 + 选项 |
| `/model` | 显示当前模型 | - |

### 计划模式

计划模式是 **read-only 探索模式**：LLM 只能读取和搜索，不能修改文件系统。

- **允许**：read_file、list_directory、grep_content、search_files 等
- **禁止**：write_file、delete_file、execute_command（有副作用）等
- **约束方式**：硬拦截，非 read_only 工具调用直接返回错误

```text
bengear> /plan
🔒 Plan mode — read-only
bengear 🔒> 帮我分析日志模块
... LLM 探索代码 + 讨论方案 ...
bengear 🔒> /plan off
🔓 Full access
bengear> 修改日志模块
... LLM 正常执行修改操作 ...
```

#### /plan 子命令

| 子命令 | 说明 |
|--------|------|
| `off` | 退出计划模式 |

输入 `/plan ` 后 Tab 自动补全子命令。

### 命令补全

- 输入 `/` 自动显示所有命令候选
- 继续输入前缀自动过滤（如 `/pl` 匹配 `/plan`）
- `Tab` / `↓` 选择下一个候选，`Shift+Tab` / `↑` 选择上一个
- 空格或回车确认选择，其他键取消补全
- `/plan ` 后自动补全子命令（含描述，参考 prompt-toolkit 风格）
- `/resume ` 后自动补全会话 ID
- 候选列表单列显示：左侧命令名（选中反色），右侧描述（灰底）

### 历史记录

- 输入历史自动保存到 `~/.bengear/history`
- 最多保存 1000 条，连续重复自动去重
- `↑`/`↓` 浏览历史，编辑后自动保存当前行

### 请求中断

- LLM 请求期间，`Ctrl+C` 触发 `SIGINT` 取消当前请求
- 请求取消后自动回到输入提示符，不会退出 REPL

## 使用示例

### 交互式聊天

```bash
./build/bengear
```

进入交互模式后，可以持续对话。输入 `/exit` 或连按两次 `Ctrl+C` 退出。

### 单次提示

```bash
./build/bengear "你好，介绍一下 BenGear"
```

执行单次对话后自动退出。

### 从 stdin 读取

```bash
cat prompt.txt | ./build/bengear --stdin
```

从标准输入读取提示内容。

### 指定模型

```bash
# 使用配置中的模型键名
./build/bengear --active-model oneapi-claude-sonnet "hello"

# 或直接指定提供商和模型
./build/bengear --provider openai --model gpt-4 "hello"
```

### 恢复会话

```bash
# 列出会话
./build/bengear session list

# 恢复指定会话
./build/bengear --session <session_id>
```

### 多级管理

```bash
# 设置用户和工作空间
./build/bengear --user alice --workspace-name my-project
```

### 显示配置

```bash
./build/bengear --show-config
```

显示解析后的完整配置，用于调试配置问题。

### 启动 Server

```bash
./build/bengear serve
```

默认监听 `0.0.0.0:8080`，Web 前端静态资源目录默认为 `./web/dist`。

### 列出技能

```bash
./build/bengear --list-skills
```

显示所有已发现的技能及其元数据。

## 配置优先级

CLI 选项优先级从高到低：

1. CLI 参数（`--api-key`、`--model` 等）
2. 环境变量（`API_KEY`、`BASE_URL` 等）
3. 配置文件（`config.json`）
4. 默认值

## 环境变量

| 变量 | 说明 |
|------|------|
| `API_KEY` | API 密钥 |
| `BASE_URL` | API 基础 URL |
| `API_URL` | 完整 API 端点 |

配置文件中可使用 `${API_KEY}` 引用环境变量。

## 输出格式

### 终端富文本渲染

默认启用 Markdown 流式渲染，LLM 响应中的 Markdown 元素会被实时渲染：

| 元素 | 渲染效果 |
|------|----------|
| 标题 `#` ~ `######` | 粗体 + 颜色 + 层级标识 |
| 表格 `\| cell \|` | 竖线分隔，隐藏分隔行 |
| 列表 `- item` / `1. item` | • / 数字前缀 |
| 引用 `> text` | │ 前缀 + dim |
| 分隔线 `---` | 全宽横线 |
| 代码块 \`\`\`lang | 语法高亮（10+ 语言） |
| **粗体** | ANSI bold |
| *斜体* | ANSI italic |
| \`行内代码\` | 背景色 + 前景色 |
| [链接](url) | 下划线 + 颜色 |

### 时间标记

消息时间以 `[HH:MM:SS]` 格式显示，dim 颜色，与内容明确区分：

```text
[14:32:05] 用户消息
[14:32:08] >> 助手回复
```

### Thinking 显示

思考过程用 dim + 粗体 + 主题色显示，与正文明确区分：

```text
╭─── thinking ───╮
│ 思考内容...
│ 更多思考...
╰──────────╯

正文回复内容...
```

### 工具调用显示

```text
┌ ⚡ tool_name
│ {"param": "value"}
└ ✓ ok  123B
```

### 非流式输出

使用 `--no-stream` 禁用流式输出，等待完整响应后一次性显示。

### 禁用 Markdown 渲染

使用 `--md-raw` 禁用终端 Markdown 渲染，直接显示原始文本：

```bash
./build/bengear --md-raw "hello"
```

适用于调试渲染问题或在不支持 ANSI 转义码的环境中使用。

## 显示配置

在 `config.json` 中通过 `display` 字段控制显示行为：

```json
{
  "display": {
    "show_thinking": true,
    "show_thinking_label": true,
    "show_tool_call": true,
    "show_tool_args": true,
    "show_tool_result": true,
    "tool_result_max_length": 200,
    "show_tool_id": false,
    "markdown_render": true,
    "syntax_highlight": true,
    "show_spinner": true,
    "show_timing": false,
    "show_token_count": false
  }
}
```

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `show_thinking` | bool | true | 是否显示思考过程 |
| `show_thinking_label` | bool | true | thinking 标签 |
| `show_tool_call` | bool | true | 是否显示工具调用 |
| `show_tool_args` | bool | true | 是否显示工具参数 |
| `show_tool_result` | bool | true | 是否显示工具结果 |
| `tool_result_max_length` | int | 200 | 结果截断长度（0=不截断） |
| `show_tool_id` | bool | false | 是否显示 tool call id |
| `markdown_render` | bool | true | 是否渲染 Markdown |
| `syntax_highlight` | bool | true | 代码块语法高亮 |
| `show_spinner` | bool | true | 等待时显示 Spinner |
| `show_timing` | bool | false | 显示耗时 |
| `show_token_count` | bool | false | 显示 token 统计 |

## 导出功能

使用 `/export` 命令导出当前会话为 Markdown 文件：

```bash
# 默认文件名：history_YYYYMMDD_HHMMSS.md
/export

# 指定文件名
/export my_session.md

# 不含工具调用
/export output.md --no-tool

# 不含思考过程
/export output.md --no-thinking

# 包含工具返回结果（默认不包含，因为通常很长）
/export output.md --with-result
```

导出选项：

| 选项 | 说明 |
|------|------|
| `--no-tool` | 不包含工具调用 |
| `--no-thinking` | 不包含思考过程 |
| `--with-result` | 包含工具返回结果 |

## 调试选项

### --show-config

显示解析后的配置，用于验证配置是否正确：

```bash
./build/bengear --show-config
```

### --async / --sync

`--async` 启用异步模式（协程），`--sync` 启用同步模式：

```bash
./build/bengear --async "hello"
```

## 相关文档

- [快速开始](quickstart.md) - 基本使用
- [配置详解](configuration.md) - 配置选项
- [工作空间](workspace.md) - 工作空间管理
