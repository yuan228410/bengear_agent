# 子 Agent 系统

BenGear 子 Agent 系统允许主 Agent 通过 LLM tool call（`delegate_task` / `delegate_tasks`）自动委派任务给子 Agent。每个子 Agent 拥有独立的 EventLoop 和 ReAct 循环，可以自主调用工具完成子任务。

## 设计哲学

```
主 Agent：决策 + 汇总，保持上下文干净
子 Agent：干脏活累活，返回精华结果
```

- 子 Agent **无系统提示词**，所有上下文由主 Agent 在 task prompt 中提供
- 子 Agent 只能看到基础工具（文件/命令/网络），无法访问记忆/灵魂/工作流/技能等
- 子 Agent 执行完整的 **ReAct 循环**（调 LLM → 执行工具 → 再调 LLM → ... → 返回结果）
- 结果通过 `output`（可能截断）和 `full_output`（完整）两级返回

## 核心概念

### 子 Agent 状态

| 状态 | 说明 |
|------|------|
| pending | 等待执行 |
| running | 正在执行 |
| success | 成功完成 |
| failed | 执行失败 |
| cancelled | 被取消 |

## 架构

```
主 Agent (ExecutionLoop)
  └── LLM 选择 delegate_task / delegate_tasks
        └── SubAgentRuntime::execute()
              ├── 独立 EventLoop 线程
              ├── 独立 ConversationHistory
              ├── 过滤后的 ToolRegistry（排除 exclude_tools）
              └── ReAct 循环：
                    LLM → 有工具调用？ → 执行 → 放回历史 → 继续
                                → 纯文本？ → 作为最终结果返回
```

## 使用方式

### 单任务委派

主 Agent 通过 `delegate_task` 工具委派单个任务：

```json
{
  "prompt": "请查询北京天气，返回简洁摘要",
  "timeout": 30000
}
```

### 并行委派

通过 `delegate_tasks` 工具并行委派多个任务：

```json
{
  "prompts": [
    "查询北京天气，返回简洁摘要",
    "查询上海天气，返回简洁摘要",
    "查询广州天气，返回简洁摘要"
  ],
  "max_parallel": 3
}
```

## 工具隔离

### 默认排除的工具（33 个）

子 Agent 默认看不到以下工具，防止污染主 Agent 状态或递归委派：

| 类别 | 工具 |
|------|------|
| 递归 delegation | `delegate_task`, `delegate_tasks` |
| 记忆/灵魂/规则/用户 | `read_memory`, `write_memory`, `recall`, `read_soul`, `write_soul`, `read_rules`, `write_rules`, `read_user`, `write_user` |
| 日记 | `append_episode`, `read_episode`, `read_episode_range` |
| 工作流（15 个） | `create_workflow`, `execute_workflow`, ... |
| TODO | `update_todo` |
| 风险操作 | `delete_file`, `env_set` |
| 工作空间 | `list_workspaces`, `create_workspace`, `remove_workspace`, `restore_workspace` |
| 历史 | `delete_history` |
| 技能管理 | `get_skill`, `install_skill`, `remove_skill`, `enable_skill`, `disable_skill`, `list_skills` |

### 子 Agent 可见的工具（14 个）

```
read_file, write_file, replace_in_file, rename_file,
list_directory, mkdir, copy_file, file_info,
search_files, grep_content, search_content,
execute_command,
http_get, http_post,
read_image,
env_get
```

可通过配置文件的 `exclude_tools` 覆盖默认排除列表。

## 自定义子 Agent

### 目录结构

```
~/.bengear/sub_agents/
├── translator.md
├── code_reviewer.md
└── research_assistant.md
```

### 文件格式

每份 `.md` 文件使用 `---` 分隔的 frontmatter：

```markdown
---
name: translator
description: Translate text between languages. Handles large documents without cluttering main context.
model: deepseek-v4-flash        # 可选，指定模型，留空用主模型
tools: http_get, http_post      # 可选，工具白名单，留空用默认工具集
---

You are a professional translator. Translate accurately, preserve formatting.
Return only the translation. Do not add commentary.
```

### frontmatter 字段

| 字段 | 必填 | 说明 |
|------|------|------|
| `name` | ✅ | 工具名 = `sub_<name>`，主 Agent 通过此名调用 |
| `description` | ❌ | 工具描述，帮助主 Agent 决定何时调用 |
| `model` | ❌ | 指定模型，不填用主 Agent 的模型 |
| `tools` | ❌ | 工具白名单，逗号分隔。填了则子 Agent **只能**使用这些工具 |
| `max_steps` | ❌ | 最大 ReAct 步数，不填用全局默认值 20 |

### 注册

启动时自动扫描 `~/.bengear/sub_agents/*.md`，每个文件注册为一个 `sub_<name>` 工具。添加文件即注册，删除文件即下线，无需重启编译。

### 执行

```
主 Agent 调用 sub_translator(prompt="翻译这段中文为英文")
  → 创建 SubAgentTask
    → task.system_prompt = 文件正文（翻译提示词）
    → task.tool_filter = frontmatter 中 tools 指定的白名单
    → task.model_override = frontmatter 中 model 指定的模型
  → SubAgentRuntime::execute()
    → ReAct 循环执行
    → 返回结果
```

## 配置

`SubAgentConfig` 嵌入 `AgentSettings`，可在 `config.json` 中配置：

```json
{
  "agent": {
    "sub_agent": {
      "max_parallel": 5,
      "default_max_steps": 20,
      "default_timeout_seconds": 120,
      "auto_summary": false,
      "max_output_chars": 0,
      "sub_agents_dir": "",
      "exclude_tools": [],
      "tool_filter_default": []
    }
  }
}
```

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `max_parallel` | int | 5 | 最大并行子 Agent 数 |
| `default_max_steps` | int | 20 | 默认最大 ReAct 步数 |
| `default_timeout_seconds` | int | 120 | 默认超时（秒） |
| `auto_summary` | bool | false | 超长输出是否截断 |
| `max_output_chars` | int | 0 | 截断字符数（0=不截断） |
| `sub_agents_dir` | string | "" | 自定义子 Agent 目录，空= `~/.bengear/sub_agents/` |
| `exclude_tools` | string[] | [] | 工具黑名单覆盖（空=代码默认33个） |
| `model_override` | string | "" | 子 Agent 模型覆盖 |
| `context_length_override` | int | 0 | 上下文长度覆盖 |
| `aggregate_parallel` | bool | true | 并行结果是否聚合 |

## 系统提示词约束

主 Agent 的系统提示词中包含以下引导，确保主 Agent 正确使用子 Agent：

> **IMPORTANT: When delegating, ALWAYS include "return a concise summary" in the sub-agent's prompt.**
> The sub-agent has no context of its own — everything it needs must be in your task prompt.

子 Agent **没有系统提示词**，完全按 task prompt 执行。主 Agent 需要在 prompt 中明确给出所有上下文和要求。

## 关键设计决策

| 决策 | 选择 | 原因 |
|------|------|------|
| 子 Agent 系统提示词 | **无** | 所有上下文由主 Agent 在 task prompt 提供 |
| 执行模型 | **ReAct 循环** | 支持多步工具调用，非一次性问答 |
| 工具隔离 | **黑名单（exclude_tools）** + 自定义白名单 | 默认安全，可配置 |
| 递归委派 | **禁止** | exclude_tools 中过滤 delegate 工具 |
| 输出控制 | output / full_output 两级 | 主 Agent 拿精华，需要时看完整数据 |
| 自定义子 Agent | **文件驱动**（.md） | 添加即注册，零代码改动 |
