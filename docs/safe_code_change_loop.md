# Safe Code Change Loop

BenGear 的代码变更链路由应用层统一编排，避免 Patch、Diff、Git、Permission、Checkpoint、Test Loop 只作为散装工具存在。

## 模块职责

- **Patch / Diff**：`patch::PatchService` 与 `patch::diff_parser` 负责解析 unified diff、预览变更、应用补丁并记录 change record。
- **Permission**：`application::CommandPipeline` / command governance 将变更请求转换为权限资源，先于任何写操作执行 permission gate。
- **Checkpoint**：`checkpoint::CheckpointService` 在应用补丁前对受影响路径创建可恢复快照。
- **Git**：`git::GitService` 在补丁落盘后读取仓库状态和 diff stat，用于给 UI/API 结构化展示当前工作区结果。
- **Test Loop**：`test_loop::TestLoopService` 执行用户提供的验证命令，保留 exit code、输出、失败分类、failure summary 与 diagnostics。
- **Runtime Events**：`RuntimeExecutionKernel` 发出 validate、authorize、checkpoint、execute、audit 阶段事件；CLI/Server 只消费事件，不耦合内部实现。

## 闭环流程

`application::SafeCodeChangeService` 是第 2 次开发新增的安全代码变更用例：

1. 解析/预览 diff，得到受影响文件。
2. 通过 `CommandPipeline` 执行 validate 与 permission gate。
3. 创建 apply 前 checkpoint。
4. 应用 patch 并记录 change id。
5. 读取 git status 和 diff stat summary。
6. 运行 test loop。
7. 输出 `SafeCodeChangeResult`，包含 preview、checkpoint、patch_apply、git_status、git_diff、test_run、execution。

## 失败路径

- 权限拒绝：停在 authorize，文件不写入。
- checkpoint 失败：停在 checkpoint/apply 前，文件不写入。
- patch apply 失败：返回结构化 patch 错误；checkpoint 已可用于审计/恢复建议。
- 测试失败：保留测试诊断信息，返回 `test_failed`，结果中携带 checkpoint/change id，可提示使用 checkpoint restore 或 patch revert。

## 可跑链路

Server API 暴露：

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

该接口复用服务端 command governance，因此权限、checkpoint、审计和 runtime execution 记录与既有 API 行为一致。
