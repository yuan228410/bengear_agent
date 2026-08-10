# Team Agent 系统

Team Agent 系统是 BenGear 的多 Agent 协作框架。支持创建由多个长活 Agent 组成的团队，每个 Agent 拥有独立的人格、记忆和工具集，通过黑板（artifacts）和消息传递协作。

## 设计哲学

```
主 LLM（调度多团队）
  ├── create_team / team_create → 加载或创建团队
  ├── team_assign → 给 Lead 派高层任务
  └── team_status → 查看所有团队进度

Lead Agent（管理一个团队）
  ├── run_team → 启动工作流
  ├── team_assign → 给 Member 派任务
  ├── team_broadcast → 群发指令
  └── team_send → 和成员沟通

Member Agent（执行具体任务）
  ├── team_send → 和 Lead/其他成员沟通
  ├── team_read_messages → 读收件箱
  └── 自己的工具（read_file / write_file 等）
```

## 架构

```
主 LLM (用户)
  │
  ├── TeamOrchestrator
  │     ├── TeamContext（黑板 + 消息队列）
  │     │
  │     ├── PersistentAgent (Lead)
  │     │     ├── 独立 EventLoop 线程
  │     │     ├── 独立 MemoryStore + Session
  │     │     ├── SubAgentRuntime（执行引擎）
  │     │     └── 过滤后的 ToolRegistry
  │     │
  │     ├── PersistentAgent (Member)
  │     │     └── ...（同上，权限更少）
  │     │
  │     └── PersistentAgent (Member)
  │           └── ...
  │
  └── EventBus → WebSocket → 前端面板
```

## 目录结构

```
~/.bengear/teams/
└── {team-name}/
    ├── team.md                    # 团队定义
    ├── stages.md                  # 可选，工作阶段定义
    └── members/
        ├── planner.md             # 成员定义
        ├── coder.md
        └── reviewer.md
```

### team.md

```markdown
---
name: dev-team
description: 软件开发团队
strategy: pipeline           # pipeline / sequential / parallel
max_concurrent: 3
members: planner,coder,reviewer
shared_tools: read_file,glob
---

# Dev Team

Planner 设计 → Coder 实现 → Reviewer 审查。
```

| frontmatter 字段 | 必填 | 说明 |
|------|------|------|
| `name` | ✅ | 团队名称 |
| `description` | ❌ | 描述信息 |
| `strategy` | ❌ | `pipeline`(默认) / `sequential` / `parallel` |
| `max_concurrent` | ❌ | 并行上限，默认 3 |
| `members` | ❌ | 成员 agent_id 列表（逗号分隔），不填则扫描 members/ 目录 |
| `shared_tools` | ❌ | 全员可见的额外工具（逗号分隔） |

### stages.md（可选）

定义 pipeline 策略的工作阶段。每行一个 stage，`|` 分隔字段，`#` 开头为注释：

```
# stage_id | description | assigned_agents | depends_on
design | 系统设计和技术选型 | planner |
implement | 编码实现 | coder | design
review | 代码审查 | planner,coder | design,implement
```

| 字段 | 说明 |
|------|------|
| `stage_id` | 阶段唯一标识 |
| `description` | 阶段描述（展示给 Agent 和前端） |
| `assigned_agents` | 执行该阶段的 Agent ID 列表（逗号分隔） |
| `depends_on` | 依赖的前置 stage ID 列表（逗号分隔，输出会注入到后续阶段） |

> 不定义 `stages.md` 时，pipeline 策略按 `members/` 顺序串行执行（fallback）。

### members/*.md

```markdown
---
name: planner
display_name: 架构师
role: lead                   # lead / member
model: deepseek-v4-flash     # 可选，覆盖模型
tools: read_file, search_content  # 可选，工具白名单
max_steps: 30                # 可选
---

你是一个软件架构师，负责系统设计和技术选型。
```

| frontmatter 字段 | 必填 | 说明 |
|------|------|------|
| `name` | ✅ | 唯一 ID |
| `display_name` | ❌ | 显示名，默认同 name |
| `role` | ✅ | `lead` 或 `member` |
| `description` | ❌ | Agent 角色描述 |
| `model` | ❌ | 模型覆盖 |
| `tools` | ❌ | 工具白名单（逗号分隔），不填用全部 |
| `max_steps` | ❌ | 最大 ReAct 步数 |
| `timeout` | ❌ | 执行超时（秒），默认 120 |
| `max_retries` | ❌ | 失败重试次数，默认 0（不重试） |

## 协作策略

| 策略 | 说明 |
|------|------|
| `pipeline` | 按 stages 定义依次执行，每个 stage 可指定多个 Agent |
| `sequential` | 所有成员依次执行，后一个看到前一个的输出 |
| `parallel` | 所有成员并行执行，受 `max_concurrent` 限制并发数 |

### 计划模式联动

`run_team` 支持传入 `plan_items` 参数，按计划项顺序分派给 Member 执行：

```json
{
  "team": "dev-team",
  "plan_items": [
    {"title": "设计架构", "description": "确定模块划分和接口", "assigned_to": "planner"},
    {"title": "实现核心", "description": "编码实现核心逻辑", "assigned_to": "coder"},
    {"title": "代码审查", "description": "审查实现质量", "assigned_to": "reviewer"}
  ]
}
```

- 每个 item 按顺序执行，前一个的输出自动注入到后一个的 context
- `assigned_to` 指定 Member agent_id，留空则按 members 顺序轮转（跳过 lead）
- 某项失败则中止，已完成的结果保留在黑板
- 结果通过 `plan_{n}_output` 和 `plan_{n}_error` 发布到黑板

## 工具列表

### 管理工具（仅主 LLM 可见）

| 工具 | 用途 |
|------|------|
| `create_team` | 从目录加载已有团队 |
| `team_create` | 动态创建团队（生成 .md 文件） |
| `team_add_member` | 添加新成员 |
| `team_remove_member` | 删除成员 |
| `team_update_member` | 修改成员配置（role/model/tools） |
| `team_update` | 修改团队配置（strategy/description） |
| `team_list` | 列出所有团队及其成员状态 |
| `team_history` | 查询团队执行历史 |

### 操作工具（Lead 可见）

| 工具 | 用途 |
|------|------|
| `run_team` | 启动团队工作流，支持 plan_items 按计划分派 |
| `team_assign` | 给指定成员派任务 |
| `team_broadcast` | 群发消息给所有成员 |

### 通信工具（全员可见）

| 工具 | 用途 |
|------|------|
| `team_send` | 给指定成员发消息 |
| `team_read_messages` | 读取收件箱 |
| `team_status` | 查看团队状态 |

## 权限模型

| | 主 LLM | Team Lead | Team Member |
|---|---|---|---|
| 管理工具 | ✅ | ❌ | ❌ |
| 操作工具 | ✅ | ✅ | ❌ |
| 通信工具 | ✅ | ✅ | ✅ |
| 查看工具 | ✅ | ✅ | ✅ |

## WebSocket 事件

团队事件通过 EventBus → EventBridge → WebSocket 推送到前端：

| 事件 | 触发时机 |
|------|---------|
| `team_start` | 团队工作流启动 |
| `team_stage` | 阶段切换 |
| `team_member` | 成员状态变化（busy / idle / sleeping） |
| `team_member_output` | 成员产出内容（实时输出） |
| `team_artifact` | 黑板 artifact 发布 |
| `team_message` | Agent 间消息传递 |

前端面板实时展示：
- 团队列表（运行中/空闲）
- 成员状态（◉ busy / ● idle / ○ sleeping）
- 当前阶段与进度

## 关键设计决策

| 决策 | 选择 | 原因 |
|------|------|------|
| Agent 间通信 | **黑板模式** + 消息队列 | artifacts 共享成果，消息队列直接对话 |
| Agent 生命周期 | **长活（可休眠/唤醒）** | 跨任务积累经验，不重复初始化 |
| 消息生命周期 | **注入后清理收件箱** | prompt 注入后立即 `clear_inbox()`，防止长期运行 context 膨胀 |
| 记忆管理 | **独立 MemoryStore** | 每个 Agent 有私有记忆，不互相干扰 |
| 工具权限 | **角色白名单 + 动态黑名单** | execute() 中对比 config.exclude_tools 动态过滤 |
| 锁安全 | **安全方法替代裸指针** | send_team_message 等方法内部持锁，消除 context() 裸指针生命周期风险 |
| 决策记录 | **结构化 DecisionRecord** | Agent 通过 record_decision() 留下结构化决策，可通过 decisions() 查询 |
| Lead 角色 | **权限标签，非管理实体** | 主 LLM 是真正的管理者，避免递归管理 |
