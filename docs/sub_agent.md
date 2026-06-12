# 子 Agent 系统

BenGear 子 Agent 系统允许主 Agent 通过 LLM tool call（`delegate_task` / `delegate_tasks`）自动委派任务给子 Agent。每个子 Agent 拥有独立的会话上下文和工具集，可以自主调用工具完成复杂子任务。

## 核心概念

### 会话类型

| 类型 | 枚举值 | 说明 |
|------|--------|------|
| 主会话 | `SessionType::main` | 用户直接交互的 Agent |
| 子 Agent 会话 | `SessionType::sub_agent` | 由 delegate 工具创建的子 Agent |
| 工作流会话 | `SessionType::workflow` | 工作流引擎创建的会话 |

子 Agent 会话通过 `parent_id` 关联主会话，在 `history.db` 中持久化。

### 子 Agent 状态

| 状态 | 说明 |
|------|------|
| `pending` | 等待执行 |
| `running` | 正在执行 |
| `completed` | 成功完成 |
| `failed` | 执行失败 |
| `cancelled` | 被取消 |
| `timeout` | 执行超时 |

## 分层架构

```
┌─────────────────────────────────────────────────────┐
│  UI 层（CLI / Web / API）                            │
│  实现 AgentCallbacks::on_sub_agent_event()           │
├─────────────────────────────────────────────────────┤
│  回调层 — AgentCallbacks::on_sub_agent_event()       │
│  纯数据、零 UI 依赖、扩展不改签名                      │
├─────────────────────────────────────────────────────┤
│  编排层 — SubAgentRuntime                            │
│  调度 / 生命周期 / 并行 / 取消 / 监控 / 聚合          │
├─────────────────────────────────────────────────────┤
│  Agent 层 — Agent + Session + ToolRegistry            │
│  run_session_async(tool_override)                    │
├─────────────────────────────────────────────────────┤
│  基础层 — SharedResources / EventLoop / ThreadPool    │
└─────────────────────────────────────────────────────┘
```

### 执行拓扑

```
主 Agent (main EventLoop)
  ├── LLM → delegate_tasks 工具调用
  ├── tool_manager_.execute_tools()
  │     └── thread pool worker
  │           └── sync_wait(wf_loop, ...)
  └─────────────────┼───────────────────
                    ▼
          wf_context EventLoop (独立线程)
          ┌──────────┐ ┌──────────┐ ┌──────────┐
          │ 子Agent 1 │ │ 子Agent 2 │ │ 子Agent 3 │
          │ Session  │ │ Session  │ │ Session  │
          │ Filtered │ │ Filtered │ │ Filtered │
          │ Registry │ │ Registry │ │ Registry │
          └──────────┘ └──────────┘ └──────────┘
          共享: ProviderClient / ThreadPool / HttpClient
```

## 使用方式

### 单任务委派

主 Agent 通过 `delegate_task` 工具委派单个任务：

```json
{
  "prompt": "查询北京天气并中文总结",
  "max_steps": 5,
  "timeout_seconds": 60
}
```

### 并行委派

通过 `delegate_tasks` 工具并行委派多个任务：

```json
{
  "tasks": [
    {"prompt": "查询北京天气"},
    {"prompt": "查询上海天气"},
    {"prompt": "查询广州天气"}
  ],
  "max_steps": 5,
  "timeout_seconds": 90
}
```

### 推测执行

指定多个模型并行竞争，取最先成功的结果：

```json
{
  "prompt": "分析这段代码的漏洞",
  "speculative_models": ["gpt-4o", "deepseek-v4-pro"]
}
```

## 事件系统

子 Agent 运行时通过 `on_sub_agent_event()` 回调发送结构化事件，UI 无关：

| 事件类型 | Payload | 说明 |
|---------|---------|------|
| `started` | `SubAgentStartedData` | 启动（含 prompt 摘要、并行序号） |
| `tool_call` | `ToolCallRequest` | 子 Agent 调用工具 |
| `tool_result` | `ToolCallResult` | 工具执行结果 |
| `token_output` | `SubAgentTokenData` | 文本输出 token |
| `completed` | `SubAgentCompletedData` | 完成（含用量统计、耗时） |
| `failed` | `SubAgentFailedData` | 失败（含错误信息） |
| `cancelled` | `std::monostate` | 被取消 |
| `timeout` | `std::monostate` | 超时 |

### SubAgentCompletedData

```cpp
struct SubAgentCompletedData {
    container::String output_summary;  // 截断至 200 字符的输出摘要
    llm::TokenUsage usage;             // 累计 token 用量
    double elapsed_seconds = 0.0;      // 执行耗时
    int tool_steps = 0;                // 工具调用步数
    bool was_truncated = false;        // 输出是否被截断
    bool was_summarized = false;       // 输出是否经 LLM 摘要
};
```

### CLI 渲染

终端渲染器以树状结构展示子 Agent 事件：

```text
┌ 🔍 查询北京天气
│ ⚡ execute_command
│ ✓ execute_command
│ 子 Agent 输出文本...
└ ✓ done · 3.2s ↑1k ↓200 steps=1
```

## 配置

`SubAgentConfig` 嵌入 `AgentSettings`，可在 `config.json` 中配置：

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `max_parallel` | int | 5 | 最大并行子 Agent 数 |
| `default_max_steps` | int | 20 | 默认最大工具调用步数 |
| `default_timeout` | ms | 120000 | 默认超时（120s） |
| `auto_summary` | bool | true | 超长输出是否 LLM 摘要 |
| `max_output_chars` | int | 4000 | 输出最大字符数 |
| `tool_filter_default` | string[] | [] | 默认工具过滤（空=全部） |
| `model_override` | string | "" | 模型覆盖 |
| `context_length_override` | int | 0 | 上下文长度覆盖（0=继承主 Agent） |
| `aggregate_parallel` | bool | true | 并行结果是否 LLM 聚合摘要 |

## 关键设计决策

| 决策 | 选择 | 原因 |
|------|------|------|
| 字符串 | 全部 `container::String` | SSO 零堆分配 |
| EventLoop | 子 Agent 用 `wf_context` | 避免主 EventLoop 死锁 |
| 事件模型 | `SubAgentEvent` + 单回调 | 扩展不改签名 |
| 工具隔离 | `create_filtered_registry()` | 避免递归委派 |
| 递归委派 | 禁止 | 过滤 delegate 工具 |
| 会话持久化 | 全量 + parent_id 关联 | 可回溯可调试 |
| 会话区分 | `session_type` + `parent_id` | 原生设计，无迁移 |
| 输出控制 | 截断 + LLM 摘要 | 保护主 Agent 上下文 |
| 结果聚合 | LLM 聚合摘要 | 减少上下文压力 |
| 推测执行 | 多模型竞争 | 提高成功率 |

## 性能要点

- Token 事件用 `container::String` SSO 覆盖绝大多数 token，零堆分配
- `create_filtered_registry()` 拷贝 `std::function` 引用计数增加，无深拷贝
- 共享 ProviderClient / HttpClient 连接池复用
- 事件回调同步调用，无线程切换开销
- Token 输出批量处理（按行着色），避免逐字符 `colorize` 调用

## 文件清单

| 文件 | 说明 |
|------|------|
| `include/ben_gear/agent/sub_agent_config.hpp` | SubAgentConfig + SessionType 枚举 |
| `include/ben_gear/agent/sub_agent.hpp` | SubAgentEvent/Result/Task/Runtime 声明 |
| `src/agent/sub_agent.cpp` | 核心运行时实现 |
| `include/ben_gear/tools/sub_agent_tools.hpp` | 工具声明 |
| `src/tools/sub_agent_tools.cpp` | delegate_task / delegate_tools 实现 |
| `tests/test_sub_agent.cpp` | 单元测试 |

## 相关文档

- [工具参考](tools-reference.md) - delegate_task / delegate_tasks 参数详解
- [回调设计](callbacks.md) - on_sub_agent_event 事件类型
- [架构设计](architecture.md) - 整体架构和设计原则
- [工作流引擎](workflow_guide.md) - DAG 任务编排
