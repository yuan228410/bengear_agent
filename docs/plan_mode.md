# 计划模式

## 概述

计划模式是 **read-only 探索模式**：LLM 只能读取和搜索，不能修改文件系统或执行有副作用的命令。

## 核心规则

- **允许**：read_file、list_directory、grep_content、search_files、file_info、http_get、memory_search、memory_read
- **禁止**：write_file、delete_file、execute_command（有副作用）、rename、mkdir 等
- **约束方式**：硬拦截 — 非 read_only 工具调用直接返回错误，不依赖 LLM 自律

## 使用方式

| 命令 | 说明 |
|------|------|
| `/plan` | 进入计划模式 |
| `/plan off` | 退出计划模式 |

## 工作流程

1. `/plan` 进入 → 🔒 Plan mode — read-only
2. LLM 自由探索：读文件、搜索代码、分析结构
3. LLM 与用户讨论方案
4. 用户满意后 `/plan off` → 🔓 Full access
5. LLM 正常执行修改操作

## 设计理念

与 Codex CLI、Claude Code 等主流 agent 一致：
- LLM 通过工具调用自然地执行任务
- 每个 tool call 就是一步执行
- 不需要从文本中解析计划步骤
- 不需要单独的步骤执行引擎
