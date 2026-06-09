# 计划模式设计文档

## 1. 概述

计划模式是 BenGear 的通用能力，支持两种入口触发步骤化执行：
- **普通模式自动规划**：LLM 判断任务复杂时自动输出 `## Plan`，系统解析后提示用户确认
- **计划模式**：用户 `/plan` 命令进入，反复讨论方案后确认执行

两种入口确认后，统一进入步骤化执行模式，按 TODO 项逐一执行。

## 2. 架构分层

```
┌────────────────────────────────────────────────────────┐
│  UI Layer（可替换：CLI / Web / API）                    │
│  实现 AgentCallbacks，负责展示和用户交互                 │
│  ChatRepl(终端) │ WebHandler(WebSocket) │ ApiServer(REST)│
├────────────────────────────────────────────────────────┤
│  Agent Layer（通用，不依赖任何 UI 终端细节）             │
│  Agent: 调度入口，持有 PlanManager，触发回调             │
│  PlanManager: 纯状态机，零 I/O 零 UI                    │
│  AgentCallbacks: 抽象接口，Agent → UI 事件通知          │
│  AgentImpl: 提示词常量，私有实现细节                     │
├────────────────────────────────────────────────────────┤
│  Foundation Layer（纯数据，零依赖）                     │
│  base/container: String, Vector, Map                   │
│  base/json: Json                                       │
│  tool/types: ToolCallRequest, ToolCallResult           │
└────────────────────────────────────────────────────────┘
```

### 依赖规则

- **严格单向**：UI → Agent → Foundation
- **UI 层不感知**：Agent 不引用任何 UI 头文件（无 terminal.h / theme.h）
- **Foundation 不感知**：Agent 层接口不依赖 UI 数据类型
- **同层可依赖**：AgentCallbacks 可引用 PlanManager/PlanStep（同层 Foundation 数据）

## 3. 模块职责

### 3.1 PlanManager（纯状态机）

| 职责 | 说明 |
|------|------|
| 模式管理 | normal / planning / executing 三态切换 |
| 步骤存储 | PlanStep 列表，结构化数据，无格式化 |
| 执行推进 | approve / advance / skip / cancel |
| 计划解析 | parse_plan_from_text() 静态函数 |
| 执行上下文 | build_execution_context() 纯文本，注入系统提示 |

**不负责**：文件读写、终端输出、ANSI 转义、用户交互

### 3.2 AgentCallbacks（抽象接口）

Agent → UI 的事件契约，所有回调传递结构化数据：

| 回调 | 参数 | 触发时机 |
|------|------|---------|
| on_token(token) | string_view | 每个 LLM token |
| on_thinking(token) | string_view | 思考内容 |
| on_tool_call(call) | ToolCallRequest | 工具调用开始 |
| on_tool_result(result) | ToolCallResult | 工具调用完成 |
| on_plan_detected(steps) | Vector\<PlanStep\> | 检测到 ## Plan |
| on_plan_mode_entered() | - | 进入计划模式 |
| on_plan_mode_exited() | - | 退出计划模式 |
| on_step_started(step, total) | PlanStep, int | 步骤开始执行 |
| on_step_completed(step) | PlanStep | 步骤完成 |
| on_step_skipped(step) | PlanStep | 步骤跳过 |
| on_plan_completed() | - | 计划全部完成 |

### 3.3 Agent（调度入口）

对外暴露的公共 API：

```cpp
class Agent {
public:
    PlanManager& plan_manager();           // 状态机引用
    Task<ChatResult> run_session_async(...); // 统一入口，内部自动处理模式
};
```

内部行为（按模式）：

| 模式 | 系统提示 | 工具调用 | 输出保存 | 回调触发 |
|------|---------|---------|---------|---------|
| normal | kPlanGuidancePrompt | 正常执行 | 检测 ## Plan → save + on_plan_detected | on_plan_detected |
| planning | kPlanModePrompt | 拦截+提示 | save → on_plan_mode_entered（首次） | on_plan_detected |
| executing | build_execution_context() | 正常执行 | 完成时 advance → on_step_completed | on_step_started/completed |

### 3.4 AgentImpl（私有实现）

写死的核心系统提示词常量：

- `kPlanGuidancePrompt`：普通模式下引导 LLM 复杂任务自动输出 ## Plan
- `kPlanModePrompt`：计划模式下禁止工具，只讨论方案

### 3.5 UI 层（ChatRepl / Web）

| 职责 | CLI 实现 | Web 实现（未来） |
|------|---------|-----------------|
| 命令处理 | /plan /approve /skip /cancel /steps | 按钮事件 |
| 步骤展示 | ANSI 格式化（○ ● ✓ ⊘） | JSON → React 组件 |
| 确认交互 | /approve 或继续讨论 | 对话框确认 |
| 模式提示 | prompt 后缀 [plan] [exec 2/5] | 顶部状态栏 |
| 步骤执行 | 自动循环 send_message | WebSocket 事件驱动 |

## 4. 模式状态机

```
                    /plan
  [Normal] ──────────────▶ [Planning]
     ▲  ▲                    │  │
     │  │                    │  │ 反复讨论
     │  │                    │  │
     │  │               LLM 输出 Plan
     │  │                    │
     │  │              /approve 确认
     │  │                    │
     │  │                    ▼
     │  │                [Executing] ──逐步执行──▶ all_done ──▶ [Normal]
     │  │                    │
     │  └── /plan off ───────┤
     │                       │
     └──── /cancel ──────────┘
     
  [Normal] + LLM 输出 ## Plan
     │
     ▼
  on_plan_detected(steps) ──▶ 用户 /approve ──▶ [Executing]
```

## 5. 数据流

```
用户输入 → UI 调用 agent.plan_manager() 查询状态
        → UI 调用 agent.run_session_async()
                ↓
         Agent 检查 plan_manager_.mode()
                ↓
    ┌── normal:
    │     注入 kPlanGuidancePrompt
    │     LLM 正常执行
    │     输出含 ## Plan → save + callbacks.on_plan_detected()
    │
    ├── planning:
    │     注入 kPlanModePrompt
    │     工具调用 → 拦截，返回提示
    │     LLM 输出文本 → save
    │
    └── executing:
          注入 build_execution_context()
          LLM 正常执行（可用工具）
          完成 → advance_step() → callbacks.on_step_completed()
                ↓
         UI 收到回调，展示给用户
```

## 6. 核心系统提示词（写死，不可配置）

### 6.1 普通模式规划引导

```
When a task is complex (3+ steps), break it down into a plan BEFORE executing.
Output your plan in this exact format:
## Plan
1. Step description
2. Step description
...
Do NOT execute any steps until the user confirms the plan.
For simple tasks (1-2 steps), just execute directly without a plan.
```

### 6.2 计划模式提示

```
You are now in PLAN MODE.
1. Discuss the solution with the user, answer questions, refine the plan
2. Output a numbered list of steps when the plan is finalized
3. Do NOT use any tools — only discuss and design
4. Format your plan as:
   ## Plan
   1. Step description
   2. Step description
   ...
Wait for the user to approve the plan before any execution.
```

## 7. 非核心提示词（文件配置）

| 文件 | 层级 | 用途 | 首次自动创建 |
|------|------|------|-------------|
| SOUL.md | global | Agent 身份和性格 | ✅ |
| USER.md | user | 用户偏好（语言、风格） | ✅ |
| RULES.md | global/workspace | 行为规范 | ❌ |
| MEMORY.md | workspace | 长期记忆 | ❌ |

组装顺序：SOUL → USER → 核心提示(写死) → RULES → 技能 → MEMORY → 工作空间 → 项目文档

## 8. 扩展预留

| 扩展点 | 当前 | 未来 |
|--------|------|------|
| PlanStep.status | pending/in_progress/completed/skipped | +failed, +paused |
| PlanStep 字段 | index, description, status, result | +dependencies, +estimated_time |
| PlanManager 持久化 | 内存，会话级 | 序列化到 DB，跨会话恢复 |
| 执行策略 | 串行 | 并行（无依赖步骤） |
| UI 交互 | CLI 命令 | Web 按钮/拖拽排序 |
| 回调扩展 | 虚方法+默认 no-op | 新增方法不破坏现有实现 |
| 多用户 | Agent per session | 共享 PlanManager pool |

## 9. 文件变更清单

| 文件 | 变更类型 | 说明 |
|------|---------|------|
| include/ben_gear/agent/plan_manager.hpp | 新增 | 纯状态机 |
| include/ben_gear/agent/callbacks.hpp | 修改 | 新增计划回调 |
| include/ben_gear/agent/agent.hpp | 修改 | 添加 plan_manager_ 成员 + 访问器 |
| include/ben_gear/agent/agent_impl.hpp | 修改 | 写死提示词常量 + build_system_prompt 改造 |
| include/ben_gear/agent/shared_resources.hpp | 修改 | 默认 SOUL/USER 文件初始化 |
| include/ben_gear/memory/context.hpp | 修改 | 组装顺序加入 USER.md |
| src/agent/agent.cpp | 修改 | 提示注入+工具拦截+输出保存+回调触发+pending标记 |
| include/ben_gear/cli/render/renderer.hpp | 修改 | 新增计划渲染虚方法 |
| src/cli/render/renderer.cpp | 修改 | TerminalRenderer 实现计划渲染 |
| src/cli/render/cli_app.cpp | 修改 | RichAgentCallbacks 桥接计划回调到 Renderer |
| include/ben_gear/cli/repl/line_editor.hpp | 修改 | 新增 set_prompt() 动态提示符 |
| include/ben_gear/cli/repl/chat_repl.hpp | 修改 | 添加 execute_current_step / finish_execution |
| src/cli/repl/chat_repl.cpp | 修改 | 命令处理+动态提示符+步骤执行循环 |
| docs/design/plan_mode.md | 新增 | 本设计文档 |
