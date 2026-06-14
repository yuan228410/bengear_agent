# Server 模式

## 概述

BenGear Server 模式提供 HTTP、WebSocket 和静态 Web UI 服务，用于在浏览器中使用多工作空间 Agent 会话、计划模式、执行 TODO、工具调用和执行事件可视化。

当前协议版本保持 `WsMessage v1`，通过新增结构化消息类型扩展能力，不升级协议版本。

## 启动

```bash
./build/bengear serve
```

Server 启动后提供：

| 能力 | 说明 |
|------|------|
| Web UI | Vue 3 + Vite 构建的浏览器界面 |
| WebSocket | 聊天、计划、TODO、执行事件、心跳 |
| REST API | 会话、配置、工作空间、文件浏览等接口 |
| 静态资源 | 内置 `web/dist` 或配置的静态目录 |
| 认证 | Bearer Token 或本地开发无认证模式 |

## Web UI 布局

Web Shell 使用三栏布局：

```text
┌────────────────────────────────────────────────────────────┐
│ TopBar：导航折叠 / 模型 / 连接 / 用户 / 登出 / 主题 / TODO │
├──────────────┬───────────────────────────────┬─────────────┤
│ NavSidebar   │ ChatView                      │ RightPanel  │
│ 工作空间/会话 │ 消息、计划草稿、输入栏          │ TODO tabs   │
└──────────────┴───────────────────────────────┴─────────────┘
```

- 左侧导航展示工作空间和会话，支持折叠为 `0` 宽度。
- 中间聊天区展示用户/助手消息、thinking、工具调用、执行事件、计划草稿和运行终态。
- 右侧面板默认折叠，可在 TopBar 展开；当前展示 TODO，预留执行/上下文 tab。
- 用户和助手消息均左对齐，计划草稿与普通消息同宽对齐。
- 历史消息可自由滚动；只有接近底部时才自动跟随输出。

## WebSocket 协议

### 消息封装

`WsMessage` v1 使用统一 envelope：

| 字段 | 说明 |
|------|------|
| `v` | 协议版本，当前固定为 `1` |
| `type` | 消息类型 |
| `session_id` | 会话 ID |
| `strings` / `ints` / `doubles` | 轻量字段，序列化时展平到顶层 |
| `data` | JSON 字符串或结构化 JSON 对象 |

后端定义：`include/ben_gear/server/ws/protocol.hpp`  
前端定义：`web/src/protocol/types.ts`、`web/src/protocol/ws-message.ts`

### 客户端到服务端

| type | 说明 |
|------|------|
| `chat` | 执行模式发送用户消息 |
| `abort` | 中止当前运行 |
| `switch` | 切换会话和工作空间 |
| `rename` | 重命名会话 |
| `delete` | 删除会话 |
| `plan_start` | 进入计划流程并生成草稿 |
| `plan_chat` | 用自然语言修订计划 |
| `plan_update_items` | 手工编辑计划条目 |
| `plan_select_option` | 选择 LLM 给出的方案 |
| `plan_confirm` | 确认计划并开始执行 |
| `plan_cancel` | 取消计划 |
| `todo_update` | 预留手工 TODO 更新 |
| `ping` | 心跳 |

### 服务端到客户端

| type | 说明 |
|------|------|
| `connected` | 连接成功，返回模型、provider、workspace 等信息 |
| `sessions` | 会话列表 |
| `token` | 流式文本 token |
| `thinking` | 思考过程增量 |
| `tool_call` / `tool_result` | 工具调用和结果 |
| `execution_event` | 统一执行事件树 |
| `plan_state` / `plan_delta` | 计划快照和增量 |
| `todo_state` / `todo_delta` | TODO 快照和增量 |
| `done` | 正常完成，可能包含 usage、outcome、retry |
| `error` | 失败终态，可能包含 outcome、retry |
| `pong` | 心跳响应 |

## 计划模式与 TODO

详见 [Web 计划模式与执行 TODO](web_plan_todo.md)。

关键行为：

- 计划草稿、TODO 状态按 `workspace + session_id` 隔离。
- 计划生成和修订阶段是 read-only；确认执行后恢复完整工具能力。
- 普通工具调用不会自动污染 TODO。
- 停止、断线或后端重启后继续执行时，保留 pending/blocked TODO，由 LLM 决定是否细化。

## 执行事件

执行事件使用统一结构 `ExecutionEvent`，覆盖：

| kind | 说明 |
|------|------|
| `chat` | 主会话运行 |
| `sub_agent` | 子 Agent 委派 |
| `workflow` | 工作流实例 |
| `task` | 工作流任务 |
| `tool` | 工具调用 |
| `approval` | 人工审批 |

前端通过 `ExecutionTimelineBlock.vue` 展示层级关系，工具调用本身通过 `ToolCallGroup.vue` 默认折叠，避免长消息流噪音。

## 会话与状态持久化

Server 会话状态包含：

- 聊天历史和消息元数据。
- 计划草稿 `PlanDraft`。
- TODO 快照 `TodoState`。
- workspace/session 绑定关系。

相关文件：

| 文件 | 职责 |
|------|------|
| `include/ben_gear/workspace/history_db.hpp` | SQLite 会话状态持久化接口 |
| `src/workspace/history_db.cpp` | 会话状态保存和恢复实现 |
| `include/ben_gear/server/session/pool.hpp` | SessionPool 和 LRU 管理 |
| `src/server/session/pool.cpp` | 会话池状态恢复与持久化 |

## 非阻塞约束

Server 模式必须遵守：

- WebSocket/HTTP loop 不直接执行 LLM、工具、计划修订或 TODO 计算。
- 请求解析后投递到 workspace agent loop。
- 回调通过 WebSocket queue 异步发送结构化消息。
- 动态上下文追加在用户提示尾部，稳定系统提示词靠前，以提升 prompt cache 命中。

## 前端构建

```bash
cd web
npm install
npm run build
```

构建产物位于 `web/dist`。

## 手工验证

1. 启动 `./build/bengear serve`。
2. 打开 Web UI 并登录本地用户。
3. 创建或切换 workspace/session。
4. 执行简单任务，确认不强制生成 TODO。
5. 执行复杂任务，确认 LLM 可创建 TODO，右侧面板实时更新。
6. 切换到计划模式，生成计划、调整计划、确认执行。
7. 手动停止或重启后端，再点击继续或输入 `继续`，确认 pending/blocked TODO 可继续推进。
8. 折叠左右面板，确认中间聊天区自动填充。
