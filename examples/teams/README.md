# Team Agent 示例

此目录包含一个完整的 Team Agent 示例配置。

## 使用方式

将 `dev_team/` 复制到 `~/.bengear/teams/`：

```bash
cp -r examples/teams/dev_team ~/.bengear/teams/
```

然后在 CLI 中使用：

```
/team run dev_team "用 C++ 实现一个简单的 LRU 缓存"
```

或通过 `run_team` 工具调用。

## 目录结构

```
dev_team/
├── team.md       — 团队定义（名称、策略、成员列表、协作指南）
├── stages.md     — 工作阶段（plan → code → review → test）
└── members/      — 各成员定义（角色、系统提示词、可用工具）
    ├── planner.md
    ├── coder.md
    ├── reviewer.md
    └── tester.md
```

## 工作流程（Pipeline）

1. **planner** — 需求分析，产出设计方案到 `plan_output`
2. **coder** — 根据设计编写代码，产出到 `code_output`
3. **reviewer** — 审查代码，产出到 `review_output`
4. **tester** — 执行测试，产出到 `test_output`
