# 计划模式

## 概述

计划模式用于在执行前让 LLM 先探索、整理方案并等待用户确认。它不是单独的步骤执行引擎，也不会在后端启发式判断任务复杂度；是否拆解任务、是否生成 TODO、是否细化步骤由 LLM 根据核心提示词和当前上下文自主决策。

计划模式支持 CLI 与 Web 两种交互：

- CLI：通过 `/plan` 进入 read-only 探索，用户确认后退出计划模式再执行。
- Web：通过输入栏模式切换生成结构化计划草稿，用户可修订、编辑、选择方案，最终确认执行。

## 核心规则

- **read-only 探索**：计划草稿生成和修订阶段只能读取、搜索和分析，不能修改文件或执行有副作用的命令。
- **硬拦截**：非 read-only 工具调用直接返回错误，不依赖 LLM 自律。
- **最终确认**：Web 计划确认后才进入执行模式；CLI 计划模式需要用户退出后再执行。
- **会话隔离**：计划状态按 `workspace + session_id` 绑定，不同会话互不影响。
- **结构化传输**：Web 使用 `plan_state` / `plan_delta` 结构化消息，不从普通 markdown 中解析计划。
- **协议不升级**：仍使用 `WsMessage v1`，只新增结构化消息类型。

## Read-only 工具范围

允许：

- `read_file`
- `list_directory`
- `grep_content`
- `search_files`
- `file_info`
- `http_get`
- `memory_search`
- `memory_read`

禁止：

- `write_file`
- `delete_file`
- `rename`
- `mkdir`
- 有副作用的 `execute_command`
- 其他会修改环境、文件系统或外部状态的工具

## CLI 使用方式

| 命令 | 说明 |
|------|------|
| `/plan` | 进入计划模式 |
| `/plan off` | 退出计划模式，恢复执行权限 |

CLI 流程：

1. `/plan` 进入 read-only 计划模式。
2. LLM 使用读取/搜索工具理解代码和需求。
3. LLM 与用户讨论方案。
4. 用户满意后 `/plan off`。
5. LLM 在执行模式下修改代码、运行测试或调用有副作用工具。

## Web 使用方式

Web 计划模式由以下组件协作：

| 模块 | 职责 |
|------|------|
| `InputBar.vue` | 切换执行/计划模式 |
| `use-plan.ts` | 管理计划状态并发送计划 WS 消息 |
| `PlanReviewBlock.vue` | 展示草稿、方案选择、条目编辑和确认执行 |
| `RightPanel.vue` / `TodoPanel.vue` | 执行后展示 TODO 状态 |

Web 流程：

1. 在输入栏切换到计划模式并发送需求。
2. 后端进入 read-only 草稿生成，返回 `plan_state`。
3. 前端在消息流当前位置展示计划草稿。
4. 如果存在多个方案，用户选择一个方案；没有多方案时不强制展示选项。
5. 用户可点击 `调整计划` 后输入修改建议，也可直接编辑条目。
6. 用户点击 `确认执行`，前端发送当前 `revision` 和条目。
7. 后端校验 revision，确认后进入执行模式。
8. 执行过程可初始化或更新 TODO，右侧面板展示进度。

## 计划数据结构

后端领域模型：`include/ben_gear/orchestration/plan.hpp`

| 结构 | 说明 |
|------|------|
| `PlanDraft` | 当前计划快照 |
| `PlanItem` | 单个计划步骤 |
| `PlanOption` | LLM 提供的备选方案 |
| `PlanManager` | 纯领域状态机 |

状态：

```text
idle → drafting → reviewing → confirmed → executing
                 ↘ failed
reviewing → cancelled
```

前端类型：`web/src/protocol/types.ts`

```ts
type PlanStatus = 'idle' | 'drafting' | 'reviewing' | 'confirmed' | 'executing' | 'cancelled' | 'failed'
```

## WebSocket 消息

客户端到服务端：

| type | 说明 |
|------|------|
| `plan_start` | 生成计划草稿 |
| `plan_chat` | 自然语言修订计划 |
| `plan_update_items` | 手工更新条目 |
| `plan_select_option` | 选择备选方案 |
| `plan_confirm` | 确认计划并执行 |
| `plan_cancel` | 取消计划 |

服务端到客户端：

| type | 说明 |
|------|------|
| `plan_state` | 完整计划快照 |
| `plan_delta` | 计划状态增量 |
| `todo_state` | 执行 TODO 快照 |
| `todo_delta` | 执行 TODO 增量 |

## 与 TODO 的关系

计划和 TODO 是两个不同层次：

| 层次 | 目的 | 生命周期 |
|------|------|----------|
| 计划 | 执行前审阅方案 | 草稿 → 确认/取消 |
| TODO | 执行中跟踪任务 | 执行开始 → 完成/中断/继续 |

计划确认后可初始化 TODO；执行模式下 LLM 也可通过 `update_todo` 维护 TODO。普通工具调用不会自动进入 TODO。

详见 [Web 计划模式与执行 TODO](web_plan_todo.md)。

## 继续执行

如果执行被用户停止、WebSocket 断开、后端重启、超时或达到工具限制，前端会根据 `RetryAdvice` 提供继续入口。

继续执行时：

- 成功项不重复执行。
- pending/blocked 项作为恢复上下文。
- running 项会在中断时被标记为 cancelled/failed/interrupted 语义。
- 是否进一步细化 TODO 仍由 LLM 决策。

## 性能约束

- 不为计划或 TODO 做额外 preflight LLM 请求。
- Server/WS loop 不阻塞等待 LLM 或工具。
- 稳定提示词放前面，动态会话/TODO/继续上下文放用户提示尾部，提升 prompt cache 命中。
- 前端状态按 session key 缓存，后台会话收到 delta 只更新缓存，不污染当前视图。

## 验证场景

### 计划草稿修订

输入复杂需求，切换计划模式，确认：

- 计划草稿在消息当前位置展示。
- 没有多方案时不显示无意义选项。
- 点击 `调整计划` 后才显示修改输入框。
- 修改后 revision 增加，确认执行时使用最新 revision。

### 执行 TODO 联动

确认计划执行后，确认：

- 右侧 TODO 面板出现任务项。
- 消息流继续正常输出。
- 工具调用折叠显示，不进入 TODO 列表。

### 停止后继续

复杂任务执行中停止或重启后端，再点击继续或输入 `继续`，确认：

- pending/blocked TODO 继续推进。
- succeeded 项不重复。
- LLM 可按需要细化 TODO。
