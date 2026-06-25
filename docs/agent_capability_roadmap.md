# AI Agent 能力补齐路线图

## 背景

当前项目已经具备 AI Coding Agent 的基础骨架：会话与工作区、LLM provider、工具调用、文件/命令/HTTP 工具、MCP、skills、sub-agent、workflow、记忆、计划模式、TODO、Web UI 等能力。

后续开发重点不应继续堆叠普通聊天功能，而应优先补齐主流 Coding Agent 在真实代码库中安全交付所需的闭环：

```text
Diff/Patch → Git → Permission → Checkpoint/Undo → Test Loop → LSP/Repo Map → Web IDE → Audit/Governance
```

本文档用于后续按路线图开发。

## 对标对象

主要参考：

- Claude Code
- Cursor Agent
- Cline
- Aider
- OpenHands
- GitHub Copilot Coding Agent

## 当前已有能力概览

| 能力 | 当前状态 |
| --- | --- |
| 多会话 / 工作区 | 已有 |
| LLM provider | 已有 OpenAI / Anthropic 等抽象 |
| 工具调用 | 已有统一 registry / manager |
| 文件工具 | 已有 read/write/delete/list/search/grep 等 |
| Shell 工具 | 已有命令执行、超时控制 |
| HTTP 工具 | 已有 GET/POST |
| Plan Mode | 已有结构化计划、方案选择、步骤决策、最终批准 |
| TODO | 已有计划确认后初始化和执行状态展示 |
| MCP | 已有基础 client 和工具注册 |
| Skills | 已有渐进式披露和安装/启停基础 |
| Sub-agent / Workflow | 已有并行委派和 workflow 基础 |
| Memory / Context | 已有记忆、压缩、裁剪和溢出恢复基础 |
| Web UI | 已有聊天、工作区、会话、计划、TODO、文件浏览基础 |
| 测试框架 | 已有 C++ 测试体系 |

## 核心缺口

当前主要短板集中在：

1. 代码修改缺少原生 diff/patch 审阅与回滚闭环。
2. Git 只能通过 shell 间接使用，缺少一等工具和状态机。
3. 权限模型较粗，缺少统一策略层和持久 allowlist。
4. 缺少 checkpoint/undo，用户安全感不足。
5. 缺少自动测试修复循环。
6. 缺少 LSP / 代码索引 / repo map，复杂项目理解仍偏 grep。
7. Web 端还不是完整 Coding Agent 工作台，缺少 diff、编辑器、terminal、测试输出。
8. 企业级审计、secret 防护、RBAC 仍需补齐。

## P0：安全改代码最小闭环

### P0.1 原生 Diff / Patch 系统

#### 目标

把文件修改从“直接覆盖文件”升级为“生成 patch → 预览 diff → 应用 → 可回滚”。

#### 建议能力

```text
preview_diff
apply_patch
revert_patch
list_changes
read_change
```

#### 功能要求

- 支持 unified diff。
- 支持多文件 patch。
- 支持 patch 冲突检测。
- 支持应用前预览。
- 支持应用后记录 change id。
- 支持按 change id 回滚。
- 支持展示新增、修改、删除、重命名。
- 支持二进制文件保护，默认不让模型直接改二进制。
- 支持与 Web UI diff viewer 对接。
- `apply_patch` / `revert_patch` 复用统一的 `CommandDescriptorFactory`，与 git/checkpoint/test-loop 保持同一治理链路。

#### 设计原则

- patch 系统应与 UI 解耦。
- Web/CLI 只消费结构化 diff 数据。
- 后端负责路径校验、冲突检测、变更记录。
- plan mode 下可允许 preview_diff，但禁止 apply_patch。

#### 验收标准

- Agent 可通过 patch 修改多个文件。
- 用户可查看本轮所有变更。
- patch 冲突时不破坏原文件。
- 任意已应用 patch 可回滚。
- 有 C++ 单测覆盖 apply/revert/conflict。

### P0.2 Git 一等能力

#### 目标

让 agent 原生理解和操作 Git 状态，而不是只通过 shell 盲调 git。

#### 建议工具

```text
git_status
git_diff
git_log
git_branch
git_checkout
git_commit
git_restore
git_stash
git_worktree
```

#### 功能要求

- 每次改动前可检查工作区是否干净。
- 能区分用户已有改动和 agent 本轮改动。
- 支持生成提交摘要。
- 支持按文件 restore。
- 支持创建 worktree / branch 隔离任务。
- 支持 commit 前展示 diff 和测试结果。

#### 验收标准

- 脏工作区下不会静默覆盖用户修改。
- Agent 可输出结构化 git status。
- Commit 前能展示将提交文件列表。
- Git 操作有权限策略控制。

### P0.3 统一权限策略层

#### 目标

统一管理 file/shell/http/mcp/git/patch 等工具权限。

#### 策略维度

```text
ask
allow
deny
allow_once
allow_session
allow_project
```

#### 策略对象

- 文件读写路径。
- 文件删除/重命名。
- shell 命令。
- HTTP 外联域名。
- MCP server / MCP tool。
- Git 写操作。
- Patch 应用。
- Plan execution。

#### 示例配置

```json
{
  "file.read": "allow",
  "file.write": "ask",
  "file.delete": "ask",
  "patch.apply": "ask",
  "git.status": "allow",
  "git.commit": "ask",
  "shell.default": "ask",
  "shell.deny": ["rm -rf /", "sudo", "chmod -R 777"],
  "http.external": "ask",
  "mcp.unknown": "ask"
}
```

#### 功能要求

- 工具调用前统一经过 policy engine。
- 支持 workspace root 限制。
- 支持 path allowlist / denylist。
- 支持 command allowlist / denylist / regex。
- 支持持久化 allowlist。
- 支持 plan mode read-only 复用同一策略层。
- 支持 Web UI 展示权限请求。

#### 验收标准

- 危险 shell 命令默认需要确认或拒绝。
- workspace 外写文件默认被拒绝或确认。
- 权限拒绝时返回结构化 tool result，不打断 LLM 协议。
- 有单测覆盖 allow/ask/deny/path/command 规则。

### P0.4 Checkpoint / Undo

#### 目标

为每轮 agent 修改提供可恢复点。

#### 建议能力

```text
create_checkpoint
list_checkpoints
restore_checkpoint
delete_checkpoint
```

#### 功能要求

- 每次工具修改前自动创建 checkpoint。
- checkpoint 可基于 git diff 或文件快照。
- 支持恢复整个会话本轮修改。
- 支持只恢复某个文件。
- 支持展示 checkpoint 包含的文件和 diff。

#### 已落地 MVP

- 新增 UI 无关的自动 mutation guard，在 `ToolCallManager` 中于工具真正执行前调用 `ToolPermissionProvider::before_tool_execution`。
- `SharedResources` 基于已注册的 `CheckpointService` 为可识别路径的修改类工具自动创建 checkpoint，包括文件写删改、patch apply/revert、checkpoint restore、git restore 等。
- checkpoint 创建失败时中止原工具执行，避免无恢复点修改工作区。
- 权限确认流支持 `permission_id`、pending 列表、一次性批准、会话批准和拒绝，审批控制工具避免自举式二次确认。

#### 验收标准

- 用户可一键撤销上一步修改。
- 测试失败后 agent 可建议回滚。
- checkpoint 与 patch/change id 关联。

### P0.5 自动测试修复循环

#### 目标

让 agent 具备“改动 → 测试 → 解析失败 → 修复 → 再测试”的工程交付闭环。

#### 状态机

```text
inspect_project
select_test_command
run_tests
parse_failure
repair
rerun_tests
summarize
```

#### 功能要求

- 自动识别项目类型：CMake、npm、pnpm、pip、go、cargo、maven 等。
- 自动选择相关测试命令。
- 支持用户配置默认测试命令。
- 解析编译错误、测试失败、lint 错误。
- 限制最大重试次数。
- 最终报告测试结果和未解决问题。

#### 验收标准

- 修改后可自动运行推荐测试。
- 失败时能提取关键错误位置。
- 修复循环不会无限重试。
- 最终总结明确：通过、失败或跳过原因。

## P1：主流 Coding Agent 体验

### P1.1 LSP / 代码智能

#### 目标

从文本搜索升级为语义代码理解。

#### 能力

```text
document_symbols
workspace_symbols
go_to_definition
find_references
diagnostics
rename_symbol
code_actions
```

#### 首批建议接入

- clangd
- tsserver / vue language server
- pyright
- gopls
- rust-analyzer

#### 验收标准

- 可获取当前文件 symbols。
- 可跳转定义和查找引用。
- 可读取 diagnostics 并提供给 agent。
- LSP 不可用时自动降级到 grep/search。

### P1.2 Repo Map / 代码索引

#### 目标

为大仓库构建轻量结构图，减少盲目 grep。

#### 能力

- 文件树摘要。
- 类/函数/符号索引。
- include/import 依赖图。
- 测试文件映射。
- 最近修改文件。
- 热点文件。
- 模块说明。

#### 阶段规划

1. 轻量 repo map：基于文件树和轻量符号扫描，LSP 不可用时也能工作。
2. 中级 repo map：依赖图和测试映射。
3. 高级 repo map：接入 LSP、embedding 语义检索和历史修改摘要。

#### 已落地 MVP

- 新增 UI 无关的 `repo_map` 核心模块，基于 workspace root 构建结构化代码库索引。
- 支持文件语言/类型识别、默认跳过 `third_party`、`node_modules`、build 产物等噪声目录，并对超大/二进制文件返回结构化 skip reason。
- 支持 C/C++、Python、TypeScript/JavaScript/Vue、Go、Rust 的轻量符号提取和 include/import/use 依赖提取。
- 复用 `GitService::status()` 标记 changed files，复用 `TestLoopService::inspect()` 注入测试命令建议。
- 新增只读工具 `repo_map_overview`、`repo_map_find_files`、`repo_map_find_symbols`、`repo_map_explain_path`，可在计划模式安全使用。

#### 验收标准

- 新会话能快速注入项目概览。
- Agent 可按 symbol 或模块定位文件。
- 大仓库下减少无效全文搜索。

### P1.3 Web Diff Viewer 和文件编辑器

#### 目标

把 Web UI 从聊天界面升级为 Coding Agent 工作台。

#### 能力

- 文件查看。
- Monaco / CodeMirror 编辑器。
- Diff viewer。
- Patch approval。
- Git status panel。
- Change list。
- 点击错误跳转文件行。

#### 验收标准

- Web 端可查看 agent 修改 diff。
- 用户可批准或拒绝 patch。
- 用户可手工编辑文件并让 agent 继续。

### P1.4 Terminal / 后台任务管理

#### 目标

支持长时间任务、开发服务和测试输出的可视化管理。

#### 能力

```text
start_task
stop_task
list_tasks
read_task_output
```

#### 功能要求

- 支持后台进程。
- 支持 stdout/stderr 实时输出。
- 支持停止任务。
- 支持 dev server 状态检测。
- 支持端口预览。
- 支持 Web terminal panel。

#### 验收标准

- 可启动并停止 npm dev server。
- 可实时查看测试输出。
- 后台任务不会阻塞主对话。

### P1.5 计划执行进度增强

#### 目标

把当前 Plan + TODO 从“展示”升级为“执行控制台”。

#### 能力

- TODO 与工具调用自动绑定。
- TODO 自动状态更新。
- 失败 TODO 标记原因。
- 支持暂停 / 继续 / 跳过。
- 支持重试单个 TODO。
- 支持重新规划某个 TODO。
- 支持执行过程中插入新任务。
- 支持计划与实际 diff 对齐。

#### 验收标准

- 每个 TODO 能看到相关工具调用和文件变更。
- 执行失败时可从失败 TODO 继续。
- 用户可在执行中调整计划。

## P2：生态和高级能力

### P2.1 Hook / Plugin Runtime

#### 目标

让项目支持业务团队定制 agent 行为。

#### Hook 点

```text
BeforeToolUse
AfterToolUse
OnSessionStart
OnUserMessage
OnPlanApproved
OnPatchApplied
OnTestsFinished
OnCommit
```

#### 功能要求

- 插件启停。
- 插件权限声明。
- 插件配置。
- 插件版本。
- 插件执行超时。
- Hook 失败策略。

### P2.2 Skills 生态增强

#### 能力

- Skill registry。
- 版本管理。
- 签名 / 校验。
- 权限声明。
- 依赖管理。
- 自动更新。
- 与项目类型绑定。
- 调用统计。

### P2.3 MCP 能力补全

#### 能力

- MCP resources。
- MCP prompts。
- MCP sampling。
- MCP server trust policy。
- MCP 工具权限。
- MCP 工具审计。
- MCP 配置 UI。
- MCP server 健康检查。

### P2.4 容器化 / 沙箱环境

#### 目标

提供 OpenHands 风格的隔离运行环境。

#### 能力

- 每任务独立容器。
- workspace mount。
- 网络访问策略。
- 端口转发。
- dev server 预览。
- 浏览器自动化。
- artifact 输出。
- 环境销毁。

### P2.5 浏览器自动化

#### 能力

- 打开页面。
- 截图。
- 点击交互。
- 检查 console error。
- 检查 network request。
- 自动跑 e2e。
- UI 截图对比。

可优先基于 Playwright 或 Playwright MCP 实现。

## P3：企业级治理

### P3.1 审计日志

#### 记录内容

- 用户。
- workspace。
- session。
- tool call。
- 输入摘要。
- 输出摘要。
- 修改文件。
- diff。
- 审批结果。
- 测试结果。
- commit 信息。

#### 要求

- 结构化存储。
- 支持查询。
- 支持导出。
- 支持脱敏。

### P3.2 Secret / 敏感信息保护

#### 能力

- 输出脱敏。
- 文件内容 secret scan。
- git diff secret scan。
- 防止把密钥发给 LLM。
- 防止 HTTP 工具外传敏感内容。
- 支持常见 key/token 规则。

### P3.3 多用户权限 / RBAC

#### 能力

- 用户角色。
- workspace 权限。
- session 权限。
- 工具权限。
- MCP server 权限。
- 审批人机制。
- 操作审计。

## 推荐开发顺序

如果按收益和依赖关系排序，建议如下：

1. 原生 diff/patch 系统。
2. Git 一等工具。
3. 统一权限策略层。
4. Checkpoint / Undo。
5. 自动测试修复循环。
6. LSP diagnostics / definition / references。
7. Web diff viewer + 文件编辑器。
8. 后台 terminal / task 管理。
9. Repo map / 代码索引。
10. 审计日志 + secret 脱敏。
11. Plan/TODO 执行控制台增强。
12. Hook / Plugin runtime。
13. Skills 生态增强。
14. MCP resources/prompts/sampling。
15. 容器化沙箱和浏览器自动化。
16. RBAC 和企业治理。

## 近期里程碑建议

### Milestone 1：Patch + Git + Permission

目标：安全改代码闭环。

包含：

- `preview_diff`
- `apply_patch`
- `revert_patch`
- `git_status`
- `git_diff`
- `git_restore`
- 基础权限策略层

当前已落地 MVP：

- 新增 UI 无关的 `patch` 核心模块，支持 unified diff 预览、应用、变更记录、`list_changes` / `read_change` 检视和按 `change_id` 回滚。
- 新增 UI 无关的 `git` 核心模块，支持结构化 `git_status`、`git_diff`、`git_log`、`git_branch`、`git_commit`、`git_restore` 和 `git_worktree`。
- 新增基础 `permission` 策略引擎，接入工具执行链路；只读工具默认允许，写操作默认返回结构化 `permission_required`，workspace 外路径和危险 shell 默认拒绝。
- 新增 UI 无关的 `checkpoint` 核心模块，支持 `create_checkpoint`、`list_checkpoints`、`read_checkpoint`、`restore_checkpoint`、`delete_checkpoint`，可按文件快照恢复并检测冲突。
- 新增 C++ 单测覆盖 patch apply/revert/conflict/path traversal、git status/diff/log/branch/commit/restore/worktree、checkpoint create/list/read/restore/delete/conflict/path traversal、permission allow/ask/deny/session allowlist。

后续增强：

- Web/CLI 交互式权限确认。
- 结构化 Diff Viewer 和 Git 面板。
- Web/CLI 上展示 `list_changes` / `read_change` 结果并联动 Diff Viewer。
- Web/CLI 对 `git_log`、`git_branch`、`git_commit`、`git_worktree` 的结构化展示和交互式确认。

交付后，agent 修改文件前后都可审阅、可回滚。

### Milestone 2：Checkpoint + Test Loop

目标：工程验证闭环。

包含：

- checkpoint 创建/恢复
- 项目测试命令识别
- 测试执行
- 失败解析
- 有限自动修复循环

当前已落地：

- Checkpoint MVP：支持按路径创建、列出、读取、恢复和删除 checkpoint。
- 恢复时默认检测文件 hash 冲突，避免静默覆盖 checkpoint 之后的修改；可通过 `force=true` 强制恢复。
- Checkpoint 工具已接入权限策略，读取型 `list/read` 默认允许，创建/恢复/删除默认需要确认。

后续增强：

- 修改类工具执行前自动创建 checkpoint。
- checkpoint 与 patch `change_id`、TODO、测试失败回滚联动。
- 自动测试命令识别和有限修复循环。

新增 Test Loop MVP：

- 新增 UI 无关的 `test_loop` 核心模块，支持 `inspect_test_commands` 和 `run_tests`。
- 支持识别 CMake、npm/pnpm/yarn、Cargo、Go、pytest 等常见测试入口并按置信度排序。
- 支持 workspace 内运行测试命令、超时控制、输出截断、exit code / elapsed time / failure summary 结构化返回。
- `inspect_test_commands` 默认允许，`run_tests` 默认需要确认，危险 shell 模式默认拒绝。

后续增强：

- 基于失败摘要自动定位文件和行号。
- 与 TODO 状态、checkpoint 自动回滚和 patch 修复闭环联动。
- 限制最大修复轮次，形成完整“测试失败 → 修复 → 复测 → 总结”状态机。

交付后，agent 可以更接近“改完并验证”。

### Milestone 3：LSP + Repo Map

目标：复杂代码库理解能力。

包含：

- LSP server 管理
- symbols/definition/references/diagnostics
- 轻量 repo map
- 测试文件映射

交付后，多文件修改准确率会明显提升。

### Milestone 4：Web Coding Workspace

目标：Web 端可审阅、编辑、运行。

包含：

- Diff viewer
- 文件编辑器
- Git status panel
- Terminal panel
- Test output panel
- TODO 与 diff/tool call 绑定

交付后，Web UI 从聊天面板升级为 Coding Agent 工作台。

## 非目标

短期内不建议优先做：

- 更多普通聊天 UI 动效。
- 无权限控制的强大 shell 能力。
- 未接入 diff/patch 前的大规模自动改代码。
- 未有审计和 secret 防护前的企业外联能力。
- 过早建设 marketplace。

## 总结

当前项目的基础设施已经比较完整，下一阶段应围绕真实研发交付补齐：

```text
安全可审阅地修改代码
可恢复地管理变更
自动验证和修复
语义理解大型代码库
在 Web 中形成完整 Coding Workspace
```

优先级最高的是：

```text
Diff/Patch + Git + Permission + Checkpoint + Test Loop
```

这五项补齐后，项目会从“有工具的 AI 助手”明显升级为“可用于真实项目交付的 AI Coding Agent”。
