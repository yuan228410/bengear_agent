# Code Intelligence Workbench

BenGear 的第 3 次规划把 Repo Map、轻量 LSP-like Code Intel、Safe Code Change Loop 和 Web Workbench 汇合到一个只读的工作台快照里。

## 边界

- **Repo Map**：`repo_map::RepoMapService` 负责仓库形态、文件筛选、符号提取、依赖关系和 Git 变更标记。
- **Code Intel**：`code_intel::CodeIntelService` 基于共享索引提供 document symbols、workspace symbols、definition、references 等 LSP-like 查询。当前实现是 lightweight index provider，不引入重型 LSP 进程依赖，但 API 结构为未来 LSP provider 保留。
- **Workbench Snapshot**：`POST /api/workbench/snapshot` 聚合 repo/code/change/quality/gate/handoff 等上下文，返回结构化 JSON，UI 只消费快照，不拥有业务状态。
- **Safe Change Integration**：工作台快照中的 `change_context`、`impact_context`、`readiness_context`、`gate_context`、`handoff_package` 为安全变更前后的影响分析、验证建议和交接审查提供证据。

## API

```http
POST /api/workbench/snapshot?workspace=default
{
  "path": "src/app.cpp",
  "symbol": "App",
  "query": "App",
  "context_lines": 8,
  "max_location_contexts": 8,
  "verification_result": {
    "success": false,
    "command": "cmake --build --preset dev-tests -j2",
    "output": "..."
  }
}
```

返回的主要字段：

- `overview`：仓库概览。
- `path` / `source_context`：选中文件解释和源码上下文。
- `document_symbols` / `workspace_symbols`：文档和仓库符号。
- `definition` / `references` / `navigation_contexts`：跳转与引用上下文。
- `change_context`：Git 状态、选中文件 diff、测试建议。
- `quality_context`：诊断和验证上下文。
- `dependency_context` / `symbol_context` / `impact_context`：依赖、符号和影响范围。
- `readiness_context` / `gate_context`：是否可继续变更或交接。
- `agent_context` / `handoff_package`：下游审查或 agent handoff 的只读证据包。

## 安全约束

Workbench snapshot 是观察型能力：

- 不执行命令。
- 不修改仓库。
- 不绕过 permission/checkpoint/test loop。
- 如果需要应用变更，必须走 `POST /api/patch/safe-change` 的治理链路。

## 验证覆盖

- `RepoMapServiceTest.*` 覆盖仓库概览、文件/符号查询、依赖和 Git enrich。
- `CodeIntelServiceTest.*` 覆盖 capabilities、document/workspace symbols、definition、references、workspace escape 防护。
- `WorkbenchApiTest.*` 覆盖 `/api/workbench/snapshot` 请求解析。
- `WorkbenchCompositionTest.*` 覆盖 repo/code/change/quality/gate/handoff 聚合快照。
