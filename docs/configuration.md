# 配置系统

## 配置文件

### 主配置文件：config.json

位置：工作区根目录

```json
{
  "active_model": "my-provider:gpt4",
  "stream": true,
  "log": {
    "level": "info",
    "output": "stdout"
  },
  "llm_request_retry": {
    "max_attempts": 5,
    "initial_delay_ms": 200,
    "max_delay_ms": 3000
  },
  "agent": {
    "max_tool_steps": 8,
    "system_prompt": "",
    "command_timeout": 30
  },
  "connection_pool": {
    "max_connections_per_host": 10,
    "idle_timeout_seconds": 30,
    "connect_timeout_seconds": 10,
    "enable_keep_alive": true,
    "enable_object_pool": true
  },
  "thread_pool": {
    "min_threads": 2,
    "max_threads": 8,
    "max_queue_size": 1024,
    "idle_timeout_ms": 5000
  },
  "mcp": {
    "read_buffer_size": 4096
  },
  "model_config": {
    "my-provider": {
      "base_url": "https://api.openai.com/v1",
      "api_key": "${API_KEY}",
      "models": [
        {
          "id": "gpt-4",
          "name": "gpt4",
          "api_mode": "openai",
          "temperature": 0.3,
          "max_tokens": 4096
        }
      ]
    }
  }
}
```

## 配置字段

### 顶层字段

| 字段　　　　　　　　| 类型　　| 必需 | 默认值 | 说明　　　　　　　　　　　　　　　　　　　　　　　　 |
| ---------------------| ---------| ------| --------| ------------------------------------------------------|
| `active_model`　　　| string　| 是　 | -　　　| 当前激活的模型引用（格式：provider_name:model_name） |
| `stream`　　　　　　| boolean | 否　 | true　 | 全局流式开关　　　　　　　　　　　　　　　　　　　　 |
| `log`　　　　　　　 | object　| 否　 | {}　　 | 日志配置　　　　　　　　　　　　　　　　　　　　　　 |
| `llm_request_retry` | object　| 否　 | {}　　 | 重试策略　　　　　　　　　　　　　　　　　　　　　　 |
| `agent`　　　　　　 | object　| 否　 | {}　　 | Agent 行为配置　　　　　　　　　　　　　　　　　　　 |
| `connection_pool`　 | object　| 否　 | {}　　 | HTTP 连接池配置　　　　　　　　　　　　　　　　　　　|
| `thread_pool`　　　 | object　| 否　 | {}　　 | 线程池配置　　　　　　　　　　　　　　　　　　　　　 |
| `mcp`　　　　　　　 | object　| 否　 | {}　　 | MCP 协议配置　　　　　　　　　　　　　　　　　　　　 |
| `model_config`　　　| object　| 是　 | -　　　| 模型分组配置　　　　　　　　　　　　　　　　　　　　 |
| `mcp_servers`　　　 | object　| 否　 | {}　　 | MCP 服务器定义　　　　　　　　　　　　　　　　　　　 |

### 日志配置 (`log`)

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `level` | string | "info" | 日志级别：trace/debug/info/warn/error |
| `output` | string | "stdout" | 输出目标：stdout/file/network |
| `file` | string | - | 日志文件路径（output=file 时必需） |
| `network_host` | string | - | 网络日志主机 |
| `network_port` | string | - | 网络日志端口 |

### 重试配置 (`llm_request_retry`)

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `max_attempts` | integer | 5 | 最大重试次数 |
| `initial_delay_ms` | integer | 200 | 初始延迟（毫秒） |
| `max_delay_ms` | integer | 3000 | 最大延迟（毫秒） |

### Agent 配置 (`agent`)

控制 Agent 核心行为，所有字段均可选。

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `max_tool_steps` | integer | 8 | 单次对话最大工具调用轮数，超出后返回 "Tool call limit reached" |
| `system_prompt` | string | "" | 自定义系统提示词，空则使用内置默认提示 |
| `command_timeout` | integer | 30 | Shell 命令执行默认超时（秒） |

### 连接池配置 (`connection_pool`)

控制 HTTP 连接池行为，所有字段均可选。适用于高延迟网络或限频 API 场景的调优。

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `max_connections_per_host` | integer | 10 | 每个主机最大保持连接数 |
| `idle_timeout_seconds` | integer | 30 | 空闲连接超时时间（秒），超时后自动回收 |
| `connect_timeout_seconds` | integer | 10 | 新建连接超时时间（秒） |
| `enable_keep_alive` | boolean | true | 是否启用 HTTP keep-alive 复用连接 |
| `enable_object_pool` | boolean | true | 是否启用对象池复用连接对象，减少堆分配开销 |

### 线程池配置 (`thread_pool`)

控制内部线程池大小和行为，所有字段均可选。适用于不同硬件配置的性能调优。

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `min_threads` | integer | 2 | 最小常驻线程数 |
| `max_threads` | integer | 8 | 最大线程数，高负载时动态扩容到此上限 |
| `max_queue_size` | integer | 1024 | 任务队列最大容量，满时提交任务会抛异常 |
| `idle_timeout_ms` | integer | 5000 | 动态扩容线程的空闲超时（毫秒），超时后自动回收 |

### MCP 配置 (`mcp`)

控制 MCP 协议客户端行为，所有字段均可选。

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `read_buffer_size` | integer | 4096 | stdio 模式下读取响应的缓冲区大小（字节），工具返回大输出时可适当调大 |

### 模型配置 (`model_config`)

模型按 provider 分组，共享 `base_url`、`api_key`、`headers` 等端点配置。`active_model` 使用 `provider_name:model_name` 格式引用模型。

```json
{
  "active_model": "oneapi:deepseek_flash",
  "model_config": {
    "oneapi": {
      "base_url": "https://oneapi-comate.baidu-int.com/v1",
      "api_key": "YOUR_API_KEY",
      "headers": { "X-Custom": "value" },
      "models": [
        {
          "id": "DeepSeek-V4-Flash",
          "name": "deepseek_flash",
          "api_mode": "openai",
          "temperature": 0.3
        }
      ]
    }
  }
}
```

#### Provider 级字段

| 字段 | 类型 | 必需 | 说明 |
|------|------|------|------|
| `base_url` | string | 是 | API 基础 URL |
| `api_key` | string | 是 | API 密钥 |
| `api_url` | string | 否 | 完整 API URL（覆盖 base_url） |
| `headers` | object | 否 | 自定义 HTTP 头 |

Provider 级字段被该 provider 下所有模型继承，model 级可覆盖。

#### Model 级字段

| 字段 | 类型 | 必需 | 默认值 | 说明 |
|------|------|------|--------|------|
| `id` | string | 是 | - | 发送给 API 的模型标识符 |
| `name` | string | 是 | - | 引用名称，用于 `active_model` 的 `model_name` 部分 |
| `api_mode` | string | 否 | "openai" | 协议：openai/anthropic |
| `api_key` | string | 否 | 继承 provider | 覆盖 provider 级 api_key |
| `base_url` | string | 否 | 继承 provider | 覆盖 provider 级 base_url |
| `temperature` | number | 否 | 0.2 | 温度参数 |
| `max_tokens` | integer | 否 | 1024 | 最大 token 数 |
| `stream` | boolean | 否 | 继承全局 | 流式响应 |
| `contextWindow` | integer | 否 | 0 | 上下文窗口大小 |
| `reasoning` | boolean | 否 | false | 是否启用推理/思考模式 |
| `headers` | object | 否 | 继承 provider | 覆盖 provider 级 headers |
| `anthropic_api_version` | string | 否 | "2026-01-01" | Anthropic API 协议版本 |
| `thinking` | object | 否 | - | Anthropic 扩展思考配置 |

#### 字段映射

| JSON 字段 | Settings 字段 | 说明 |
|-----------|--------------|------|
| `id` | `model` | API 模型标识 |
| `name` | `display_name` | 显示/引用名 |
| `contextWindow` | `context_length` | 上下文窗口 |

#### 继承规则

1. `base_url`、`api_key`、`headers` 从 provider 继承，model 级可覆盖
2. `stream` 从全局 `stream` 继承（model 未指定时）
3. `log`、`llm_request_retry`、`agent`、`connection_pool`、`thread_pool`、`mcp` 从全局配置继承

#### `active_model` 格式

```
provider_name:model_name
```

例如：`oneapi:deepseek_flash`、`direct-openai:gpt4o`

#### Anthropic 扩展思考配置 (`thinking`)

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `budget_tokens` | integer | 10000 | 思考 token 预算 |
| `enabled` | boolean | false | 是否启用扩展思考 |
| `type` | string | "adaptive" | 思考模式 |

### MCP 服务器配置 (`mcp_servers`)

每个服务器是一个键值对，键为服务器名称，值为服务器配置：

| 字段 | 类型 | 必需 | 默认值 | 说明 |
|------|------|------|--------|------|
| `command` | string | 否 | - | stdio 模式启动命令 |
| `args` | array | 否 | [] | 命令参数 |
| `env` | object | 否 | {} | 环境变量 |
| `url` | string | 否 | - | HTTP 模式 URL（暂未实现） |
| `disabled` | boolean | 否 | false | 是否禁用此服务器 |

## URL 自动补全

### OpenAI 协议

```
base_url = "https://api.openai.com"
→ 实际 URL = "https://api.openai.com/v1/chat/completions"
```

### Anthropic 协议

```
base_url = "https://api.anthropic.com"
→ 实际 URL = "https://api.anthropic.com/v1/messages"
```

### 已包含 /v1

```
base_url = "https://api.openai.com/v1"
→ 实际 URL = "https://api.openai.com/v1/chat/completions"
```

### 完整 URL

```
api_url = "https://custom.api.com/endpoint"
→ 使用完整 URL，忽略 base_url
```

## 环境变量

支持在配置值中使用环境变量：

```json
{
  "api_key": "${OPENAI_API_KEY}"
}
```

环境变量会在运行时替换。

## 配置加载顺序

1. 默认值
2. 全局配置（`~/.bengear/global.conf`）
3. 用户配置（`~/.bengear.conf`）
4. 工作区配置（`./config.json`）
5. 命令行参数

后加载的配置会覆盖先加载的。

## 命令行覆盖

```bash
# 覆盖活跃模型
./bengear --active-model oneapi:deepseek_flash "hello"

# 覆盖 API URL
./bengear --api-url https://custom.api.com/endpoint "hello"

# 覆盖 API 密钥
./bengear --api-key sk-xxx "hello"

# 覆盖提供商
./bengear --provider anthropic "hello"
```

## 配置示例

### 单 provider 多模型

```json
{
  "active_model": "openai:gpt4",
  "model_config": {
    "openai": {
      "base_url": "https://api.openai.com/v1",
      "api_key": "${OPENAI_API_KEY}",
      "models": [
        {
          "id": "gpt-4-turbo-preview",
          "name": "gpt4",
          "api_mode": "openai",
          "temperature": 0.7,
          "max_tokens": 4096
        }
      ]
    }
  }
}
```

### 多 provider 混合

```json
{
  "active_model": "oneapi:deepseek",
  "model_config": {
    "oneapi": {
      "base_url": "https://oneapi.example.com/v1",
      "api_key": "${API_KEY}",
      "headers": { "X-Custom-Header": "value" },
      "models": [
        { "id": "deepseek-chat", "name": "deepseek", "api_mode": "openai", "temperature": 0.3 },
        { "id": "claude-3-5-sonnet-20241022", "name": "claude", "api_mode": "anthropic" }
      ]
    },
    "direct": {
      "base_url": "https://api.anthropic.com/v1",
      "api_key": "${ANTHROPIC_API_KEY}",
      "models": [
        { "id": "claude-3-5-sonnet-20241022", "name": "sonnet", "api_mode": "anthropic" }
      ]
    }
  }
}
```

## 配置验证

### 必需字段检查

```cpp
if (api_key.empty()) {
    throw std::runtime_error(
        "Missing API key. Please set it via:\n"
        "  1. Environment variable: export BEN_GEAR_API_KEY=your_key\n"
        "  2. Config file: {\"api_key\": \"your_key\"}\n"
        "  3. CLI argument: --api-key your_key"
    );
}
```

### URL 格式验证

```cpp
if (!base_url.empty() && base_url.find("://") == std::string::npos) {
    throw std::runtime_error("invalid base_url format");
}
```

### 数值范围检查

```cpp
if (temperature < 0.0 || temperature > 2.0) {
    throw std::runtime_error("temperature must be between 0.0 and 2.0");
}
```

## 配置调试

显示解析后的配置：

```bash
./bengear --show-config
```

输出示例：

```text
provider=openai
base_url=https://api.openai.com/v1
api_url=https://api.openai.com/v1/chat/completions
model=gpt-4
display_name=gpt4
stream=true
reasoning=false
llm_request_retry.max_attempts=5
llm_request_retry.initial_delay_ms=200
llm_request_retry.max_delay_ms=3000
log.level=info
log.output=stdout
max_tokens=4096
temperature=0.7
api_key=<set>
agent.max_tool_steps=8
agent.system_prompt=<default>
agent.command_timeout=30
connection_pool.max_connections_per_host=10
connection_pool.idle_timeout_seconds=30
connection_pool.connect_timeout_seconds=10
connection_pool.enable_keep_alive=true
connection_pool.enable_object_pool=true
thread_pool.min_threads=2
thread_pool.max_threads=8
thread_pool.max_queue_size=1024
mcp.read_buffer_size=4096
anthropic_api_version=<default>
```

## 最佳实践

1. **密钥管理**
   - 使用环境变量存储 API 密钥
   - 不要将密钥提交到版本控制
   - 使用 `.gitignore` 忽略 `config.json`

2. **配置分离**
   - 使用 `config-example.json` 作为模板
   - 每个环境使用不同的配置文件
   - 敏感信息使用环境变量

3. **模型管理**
   - 为不同用途配置不同模型
   - 使用有意义的模型名称
   - 记录模型配置的用途

4. **性能优化**
   - 合理设置 `max_tokens`
   - 根据需求调整 `temperature`
   - 启用流式响应提升体验
   - 高延迟网络下增大 `connection_pool.connect_timeout_seconds`
   - 限频 API 下减少 `connection_pool.max_connections_per_host`
   - 大输出 MCP 工具下增大 `mcp.read_buffer_size`

5. **错误处理**
   - 配置重试策略
   - 设置合理的超时
   - 记录错误日志
