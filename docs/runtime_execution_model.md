# Runtime Execution Model

BenGear runtime execution is the shared kernel boundary for command-like work. It keeps UI, tools, workflow, and future agent orchestration from each owning separate permission/checkpoint/audit logic.

## Core objects

- `ExecutionRequest`: caller intent plus a `CommandDescriptor`, `RuntimeBoundary`, and optional `dry_run` flag.
- `ExecutionPlan`: deterministic, serializable plan derived from the request.
- `ExecutionStep`: one guarded stage in the plan.
- `ExecutionResult`: final status, output, plan, and trace.
- `ExecutionTraceEvent`: per-step evidence for replay, debugging, audit, and Workbench display.
- `RuntimeExecutionKernel`: executes the guarded sequence using injectable hooks.

## Standard guarded sequence

```text
ExecutionRequest
→ validate
→ authorize
→ checkpoint       # trace-visible; may be a no-op for non-mutating operations
→ execute
→ audit
→ ExecutionResult
```

The execution kernel does not know concrete services. It accepts hooks for validation, authorization, checkpoint creation, execution, and audit append. This keeps the model reusable by API routes, tools, Workbench, and workflow runners.

## Dry run

`dry_run=true` builds the same plan and emits planned trace entries, but does not call mutation/execution hooks. This is suitable for UI previews, agent handoff, and permission explanation.

## Failure behavior

- Validation failure stops before authorization.
- Authorization failure stops before checkpoint and execute.
- Checkpoint failure stops before execute.
- Execute failure still triggers audit with a failed result.
- Successful execution appends audit after output capture.

## Governance adapter

`make_runtime_execution_kernel(CommandGovernanceConfig)` adapts existing command governance into the runtime kernel:

- permission check receives `runtime_boundary`, `runtime_operation`, and `permission_gate`.
- checkpoint hook reuses command checkpoint creation.
- audit event category is `runtime_execution` and includes the serialized execution result and runtime boundary.

## Current integration

`CommandPipeline` now executes through `RuntimeExecutionKernel` while preserving existing typed API return values. This means the mutation paths that already delegate to the application command pipeline share the same guarded execution trace:

- Patch mutations: `patch.apply`, `patch.revert`;
- Git mutations: branch create/delete/switch, restore, commit, worktree mutations;
- Checkpoint mutations: restore/delete;
- Test runs: `test.run` command execution.

`CommandGovernanceConfig` supplies the runtime hooks:

- validation remains command-pipeline compatible;
- authorization reuses command permission checks and carries `runtime_operation` / `permission_gate` metadata;
- checkpoint reuses command checkpoint creation and is represented as a trace step even when it is a no-op for non-mutating operations;
- audit event category is `runtime_execution` and includes the serialized execution result and runtime boundary.

The integration does not change UI behavior, does not start agents automatically, and keeps existing API response shapes compatible while adding plan/trace evidence to the governance boundary.
