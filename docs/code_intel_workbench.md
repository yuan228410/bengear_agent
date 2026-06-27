# Code Intelligence Workbench

BenGear 的第 3 次规划把 Repo Map、轻量 LSP-like Code Intel、Safe Code Change Loop 和 Web Workbench 汇合到一个只读的工作台快照里。

## 边界

- **Repo Map**：`repo_map::RepoMapService` 负责仓库形态、文件筛选、符号提取、依赖关系和 Git 变更标记。
- **Code Intel**：`code_intel::CodeIntelService` 基于共享索引提供 document symbols、workspace symbols、definition、references 等 LSP-like 查询。当前实现是 lightweight index provider，不引入重型 LSP 进程依赖，但 API 结构为未来 LSP provider 保留。
- **Workbench Snapshot**：`POST /api/workbench/snapshot` 聚合 repo/code/change/quality/gate/handoff 等上下文，返回结构化 JSON，UI 只消费快照，不拥有业务状态。
- **Safe Change Integration**：工作台快照中的 `change_context`、`impact_context`、`readiness_context`、`gate_context`、`handoff_package` 为安全变更前后的影响分析、验证建议和交接审查提供证据。

## API

### Workbench Snapshot

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

### Safe Code Change with Repo Intelligence

```http
POST /api/patch/safe-change
{
  "session_id": "...",
  "workspace": "default",
  "unified_diff": "--- a/file...",
  "description": "...",
  "test_command": "cmake --build --preset dev-tests -j2 && ctest --preset dev-tests",
  "test_cwd": ".",
  "test_timeout_seconds": 120,
  "test_max_output_bytes": 60000
}
```

返回的 `repo_intelligence` 字段包含：

- `affected_paths`：受影响的文件路径列表。
- `symbols`：每个受影响文件中的符号列表。
- `impacts`：每个文件的影响分析（符号数量、依赖数量、被依赖数量、相关测试数量）。
- `related_tests`：相关测试文件列表。
- `test_suggestions`：从仓库概览提取的测试建议。

## 数据流

### Workbench Snapshot 数据流

```
Request → WorkspaceApplicationServices
        → CodeIntelligenceIndex (共享 RequestIndexSession)
        → RepoMapService.snapshot()
        → CodeIntelService (document_symbols, workspace_symbols, definition, references)
        → workbench_contexts (navigation, symbol, dependency, impact, readiness, gate, handoff)
        → JSON Response
```

### Safe Code Change 数据流

```
Request → SafeCodeChangeService.run()
        → PatchService.preview() → affected_paths
        → CodeIntelligenceIndex (可选，注入时)
            → document_symbols for each affected_path
            → explain_path for dependencies and related tests
            → overview for test_suggestions
            → repo_intelligence JSON
        → CommandPipeline.execute()
            → Permission Gate
            → CheckpointService.create()
            → PatchService.apply()
            → GitService.status() / diff()
            → TestLoopService.run()
        → SafeCodeChangeResult (包含 repo_intelligence)
```

## 安全约束

Workbench snapshot 是观察型能力：

- 不执行命令。
- 不修改仓库。
- 不绕过 permission/checkpoint/test loop。
- 如果需要应用变更，必须走 `POST /api/patch/safe-change` 的治理链路。

## 实现细节

### CodeIntelligenceIndex

`code_intel::CodeIntelligenceIndex` 是 Request-scoped facade，统一封装了：

- `repo_map::RepoMapService`：文件树、符号、依赖关系。
- `code_intel::CodeIntelService`：LSP-like 查询。
- `workspace_index::RequestIndexSession`：请求级索引缓存。

关键方法：

- `overview()`：仓库概览（文件数、符号数、语言分布、测试建议）。
- `explain_path(path)`：文件解释（符号、依赖、被依赖、相关测试）。
- `find_files(query, kind, language)`：文件查询。
- `workspace_symbols(query, kind, language)`：仓库级符号查询。
- `document_symbols(path)`：文档符号查询。
- `definition(query)`：定义跳转。
- `references(query)`：引用查询。

### WorkspaceApplicationServices

`server::composition::WorkspaceApplicationServices` 管理请求级服务实例：

```cpp
auto services = WorkspaceApplicationServices(ws_ctx);
auto intelligence = services.code_intelligence_index();
auto git = services.git();
auto checkpoint = services.checkpoint();
auto test_loop = services.test_loop();
```

服务实例在请求生命周期内缓存，共享索引。

### RuntimeEvent 贯通

`SafeCodeChangeService` 接受可选的 `core::RuntimeEventSink`，但当前实现不强制要求。事件通过 `CommandPipeline` 流向：

1. `validate`：验证 diff 格式。
2. `authorize`：权限检查。
3. `checkpoint`：创建快照。
4. `execute`：应用补丁。
5. `test`：运行测试。
6. `audit`：记录审计。

## 验证覆盖

- `RepoMapServiceTest.*` 覆盖仓库概览、文件/符号查询、依赖和 Git enrich。
- `CodeIntelServiceTest.*` 覆盖 capabilities、document/workspace symbols、definition、references、workspace escape 防护。
- `WorkspaceIndexServiceTest.*` 覆盖索引缓存、请求会话共享。
- `WorkbenchApiTest.*` 覆盖 `/api/workbench/snapshot` 请求解析。
- `WorkbenchCompositionTest.*` 覆盖 repo/code/change/quality/gate/handoff 聚合快照。
- `PatchApiTest.*` 覆盖 `/api/patch/safe-change` 请求处理和 repo_intelligence 填充。

## 扩展点

### 未来 LSP Provider

当前 `CodeIntelService.capabilities()` 返回：

```json
{
  "provider": "indexed",
  "real_lsp": false,
  "capabilities": {
    "document_symbols": true,
    "workspace_symbols": true,
    "definition": true,
    "references": true,
    "hover": false,
    "completion": false
  }
}
```

未来可替换为真实 LSP client，保持 API 契约不变。

### Repo Intelligence 增强

`SafeCodeChangeResult.repo_intelligence` 字段为扩展预留了空间：

- `call_graph`：调用图分析。
- `data_flow`：数据流分析。
- `test_coverage`：测试覆盖率关联。
- `breaking_changes`：API 破坏性变更检测。

## 相关文档

- `safe_code_change_loop.md`：Safe Code Change Loop 设计。
- `workbench_handoff_package_v1.md`：Handoff Package 规范。
- `runtime_execution_model.md`：Runtime Execution 模型。
- `architecture.md`：整体架构。
