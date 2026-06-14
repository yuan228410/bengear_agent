# Web 计划模式与执行 TODO

## 定位

Web 端计划模式与执行 TODO 是 Server 模式下的结构化交互能力：计划用于执行前审阅和调整方案，TODO 用于执行中展示复杂任务拆解和进度。二者均按 `workspace + session_id` 隔离，不是全局状态，也不绑定具体 UI 展示。

核心原则：

- **协议仍为 v1**：继续使用 `WsMessage` v1，新增结构化 `type`，不升级协议版本。
- **LLM 自主决策**：是否拆解任务、是否更新 TODO 由核心提示词引导 LLM 决策；后端不做启发式预处理或额外 preflight LLM 调用。
- **不阻塞核心 loop**：WebSocket/HTTP loop 只解析、投递和发送消息；计划生成、修订、确认执行、TODO 更新都进入 workspace agent loop。
- **结构化数据优先**：计划、TODO、执行事件均使用领域结构和 JSON 序列化，前端只负责渲染与交互。
- **会话可恢复**：计划草稿和 TODO 状态持久化到会话状态，切换会话或重启后端后可恢复。

## 后端模型

### Plan

领域模型位于：

- `include/ben_gear/orchestration/plan.hpp`
- `src/orchestration/plan.cpp`
- `include/ben_gear/orchestration/plan_parser.hpp`
- `src/orchestration/plan_parser.cpp`

关键结构：

| 结构 | 说明 |
|------|------|
| `PlanDraft` | 当前计划快照，包含 `plan_id`、`session_id`、`workspace`、`title`、`objective`、`revision`、`status`、`items`、`options` |
| `PlanItem` | 单个执行步骤，包含 `id`、`title`、`description`、`order`、`choices`、`custom_note` |
| `PlanOption` | LLM 提供的备选方案，只有存在真实选择时才展示 |
| `PlanManager` | 纯领域状态机，不直接依赖 WebSocket、UI 或 LLM Provider |

状态流转：

```text
idle → drafting → reviewing → confirmed → executing
                 ↘ failed
reviewing → cancelled
```

计划模式是 read-only 阶段：草稿生成/修订期间工具能力被限制为读取和搜索；用户确认后才进入执行模式。

### TODO

领域模型位于：

- `include/ben_gear/orchestration/todo.hpp`
- `src/orchestration/todo.cpp`

关键结构：

| 结构 | 说明 |
|------|------|
| `TodoState` | 当前会话 TODO 快照，包含 `session_id`、`workspace`、`plan_id`、`items` |
| `TodoItem` | 单个任务项，包含 `todo_id`、`title`、`active_form`、`status`、`progress`、`result_summary` |
| `TodoDelta` | 单项增量更新，用于 WebSocket 推送 |
| `TodoManager` | 管理初始化、upsert、状态更新、运行中项终态标记和恢复 |

TODO 只表达任务拆解和进度，不再把普通工具调用自动写入 TODO。`read_file`、`list_directory` 等工具调用只显示在消息里的工具调用折叠块或执行事件中。

## WebSocket 消息

协议仍为 `v: 1`。计划/TODO 使用专门消息类型，不复用普通 markdown 文本。

### 客户端 → 服务端

| type | 说明 | data |
|------|------|------|
| `plan_start` | 基于用户输入生成计划草稿 | `{ prompt, note }` |
| `plan_chat` | 用户用自然语言要求调整计划 | `{ note }` |
| `plan_update_items` | 用户手工编辑计划条目 | `{ items }` |
| `plan_select_option` | 选择 LLM 提供的备选方案 | `{ option_id }` |
| `plan_confirm` | 确认计划并进入执行 | `{ revision, items }` |
| `plan_cancel` | 取消当前计划 | `{}` |
| `todo_update` | 预留手工 TODO 调整入口 | `{ item }` |

### 服务端 → 客户端

| type | 说明 | data |
|------|------|------|
| `plan_state` | 完整计划快照 | `PlanState` |
| `plan_delta` | 计划增量或状态变化 | JSON object |
| `todo_state` | 完整 TODO 快照 | `TodoState` |
| `todo_delta` | 单项 TODO 更新 | `TodoDelta` |
| `execution_event` | 统一执行事件树 | `ExecutionEvent` |
| `done` / `error` | 运行终态，包含 `RunOutcome` 和 `RetryAdvice` | JSON object |

前端协议类型位于：

- `web/src/protocol/types.ts`
- `web/src/protocol/ws-message.ts`

## 前端状态分层

| 模块 | 职责 |
|------|------|
| `web/src/composables/use-chat.ts` | 聊天消息、流式输出、运行终态、重试/继续入口 |
| `web/src/composables/use-plan.ts` | 按 `workspace:session_id` 管理计划状态，发送计划相关 WS 消息 |
| `web/src/composables/use-todos.ts` | 按 `workspace:session_id` 管理 TODO 快照和 delta |
| `web/src/utils/execution-events.ts` | 统一执行事件解析和归并 |

聊天、计划、TODO 三类状态相互独立，避免消息流状态继续膨胀。

## Web 交互

### 布局

当前 Shell 是三栏 Grid：

```text
┌────────────────────────────────────────────────────────────┐
│ TopBar：导航折叠 / 模型 / 连接 / 用户 / 登出 / 主题 / TODO │
├──────────────┬───────────────────────────────┬─────────────┤
│ NavSidebar   │ ChatView                      │ RightPanel  │
│ 工作空间/会话 │ 消息、计划草稿、输入栏          │ TODO tabs   │
└──────────────┴───────────────────────────────┴─────────────┘
```

- 左侧导航和右侧面板折叠后对应列宽为 `0`，中间聊天区自动填满。
- 右侧面板默认折叠，展开/收起入口在 TopBar，避免孤立悬浮箭头。
- 右侧面板使用 tab 结构，当前展示 TODO，预留执行/上下文等扩展页。
- 用户消息和助手消息均左对齐，计划草稿卡片与普通消息同宽对齐。

### 计划草稿

组件：`web/src/components/chat/PlanReviewBlock.vue`

行为：

- 计划草稿以内嵌消息块展示，不固定在消息流顶部。
- 如果 LLM 提供多个方案，方案以纵向全宽卡片展示。
- 默认只显示 `调整计划`、`取消`、`确认执行`；修改输入框只在用户点击 `调整计划` 后显示。
- 用户可编辑条目标题/描述、选择方案或发送自然语言修改建议。
- 确认执行时携带当前 `revision`，后端校验过期 revision。

### TODO 面板

组件：`web/src/components/chat/RightPanel.vue`、`TodoPanel.vue`

行为：

- TODO 仅显示计划条目和 LLM 通过 `update_todo` 明确维护的任务项。
- 普通工具调用不进入 TODO，避免 `read_file/list_directory` 污染任务列表。
- 执行成功时可批量终结运行中项；手动停止、错误、断线或后端重启只标记 running 项，保留 pending/blocked 项用于继续执行。

### 工具调用与执行事件

组件：

- `ToolCallGroup.vue`
- `ExecutionTimelineBlock.vue`
- `ThinkingBlock.vue`
- `OutcomeBlock.vue`

行为：

- 连续工具调用默认折叠成一个工具组，减少消息流噪音。
- 统一执行事件树展示 sub-agent、workflow、task、tool、approval 等层级。
- 思考、工具、执行块采用轻边框和低对比背景，不使用阴影或厚重强调条。

### 滚动体验

- 用户接近底部时，新消息自动跟随输出。
- 用户滚动查看历史时，不强制跳到底部。
- 离开底部后显示居中的向下按钮，点击回到底部。

## 继续与重试

运行终态使用 `RunOutcome` 和 `RetryAdvice` 表达，不把错误文本硬编码到 UI。

可继续的典型场景：

| 场景 | outcome | retry mode |
|------|---------|------------|
| 用户手动停止 | `cancelled/user_cancelled` | `continue_run` |
| WebSocket 断开或后端重启 | `interrupted/transport_error` | `continue_run` |
| 超时 | `interrupted/timeout` | `continue_run` |
| 工具次数限制 | `interrupted/tool_limit` | `continue_run` |

用户点击继续或手动输入“继续/continue/resume”时，后端会把当前 TODO 状态作为恢复上下文追加到用户提示尾部：继续 pending/blocked 项，避免重复 succeeded 项，并由 LLM 决定是否细化 TODO。

## 提示词与性能

- 固定的模式纪律放在系统提示词稳定区域，便于 prompt cache 命中。
- 会话相关、TODO 摘要、继续执行说明等动态内容放在用户提示尾部。
- 后端不为 TODO/计划做启发式复杂度判断，不额外发起预判 LLM 请求。
- WebSocket 回调只做结构化消息发送和状态持久化，不在核心 loop 中阻塞等待 LLM/工具。

## 验证场景

### 复杂任务拆解

输入：

```text
mac 下使用 C++ 实现高性能 web server
```

预期：

- 执行模式下 LLM 判断任务复杂时调用 `update_todo` 创建任务项。
- 右侧 TODO 面板出现架构设计、网络模型、HTTP/WS、性能验证等任务项。
- 消息流仍正常输出，不只更新 TODO。

### 简单任务

输入：

```text
把 README 里的一个错别字修正
```

预期：

- LLM 可直接执行，不强制创建 TODO。
- 右侧 TODO 面板可保持为空或不变。

### 计划模式修订

操作：

1. 输入复杂需求并切到计划模式。
2. 等待计划草稿出现。
3. 点击 `调整计划`，输入修改建议。
4. 选择方案或编辑条目。
5. 点击 `确认执行`。

预期：

- 草稿在当前位置展示，不跳到消息顶部。
- 修改输入框只按需出现。
- 确认后进入执行，TODO 面板与执行状态同步。

### 停止后继续

操作：

1. 启动复杂任务。
2. 手动停止或重启后端。
3. 点击继续，或输入 `继续`。

预期：

- 已成功项不重复执行。
- pending/blocked 项继续推进。
- LLM 可按需要细化 TODO，而不是后端强制拆分。

## 相关文件

| 领域 | 文件 |
|------|------|
| 后端计划 | `include/ben_gear/orchestration/plan.hpp`, `src/orchestration/plan.cpp`, `src/orchestration/plan_parser.cpp` |
| 后端 TODO | `include/ben_gear/orchestration/todo.hpp`, `src/orchestration/todo.cpp` |
| WS 协议 | `include/ben_gear/server/ws/protocol.hpp`, `src/server/ws/protocol.cpp` |
| Server 分发 | `src/server/core/server.cpp` |
| 回调桥接 | `include/ben_gear/server/callback/server_callbacks.hpp`, `src/server/callback/server_callbacks.cpp` |
| 前端协议 | `web/src/protocol/types.ts`, `web/src/protocol/ws-message.ts` |
| 前端状态 | `web/src/composables/use-chat.ts`, `web/src/composables/use-plan.ts`, `web/src/composables/use-todos.ts` |
| 前端 UI | `web/src/components/chat/PlanReviewBlock.vue`, `TodoPanel.vue`, `RightPanel.vue`, `ToolCallGroup.vue`, `ExecutionTimelineBlock.vue` |
