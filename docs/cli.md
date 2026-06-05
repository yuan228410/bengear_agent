# CLI 参考

## 全局选项

```text
--config <path>                 JSON 模型配置路径
--active-model <name>           模型键名（格式：provider:model）
--api-key <key>                 API 密钥
--api-url <url>                 完整 API 端点 URL
--base-url <url>                API 基础 URL
--provider <name>               提供商：openai 或 anthropic
--model <name>                  模型名称
--stream / --no-stream          启用/禁用流式响应
--chat                          进入交互聊天模式
--stdin                         从 stdin 读取提示
--show-config                   显示解析后的配置
--list-skills                   列出所有可用技能
--new-session                   强制创建新会话
--async                         启用异步模式
--username <name>               设置用户名
--workspace <name>              设置工作空间名
--role <name>                   设置角色名
--session <id>                  恢复指定会话
--context-length <n>            设置上下文窗口大小
--reasoning                     启用推理/思考模式
-h, --help                      显示帮助
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

## 使用示例

### 交互式聊天

```bash
./build/bengear
```

进入交互模式后，可以持续对话。输入 `exit` 或 `quit` 退出。

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
./build/bengear --username alice --workspace my-project

# 设置角色
./build/bengear --role lead
```

### 显示配置

```bash
./build/bengear --show-config
```

显示解析后的完整配置，用于调试配置问题。

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

### 流式输出

默认启用流式输出（`--stream`），实时显示 LLM 响应：

```text
[thinking] 思考内容... [/thinking]
实际回复内容...
[tool call] tool_name id=xxx args={...}
[tool result] ok id=xxx bytes=123
```

### 非流式输出

使用 `--no-stream` 禁用流式输出，等待完整响应后一次性显示。

## 调试选项

### --show-config

显示解析后的配置，用于验证配置是否正确：

```bash
./build/bengear --show-config
```

### --async

启用异步模式，使用协程处理请求：

```bash
./build/bengear --async "hello"
```

## 相关文档

- [快速开始](quickstart.md) - 基本使用
- [配置详解](configuration.md) - 配置选项
- [工作空间](workspace.md) - 工作空间管理
