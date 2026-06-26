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
→ checkpoint       # only when the operation mutates workspace/repository state
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

## Current boundary

This kernel is intentionally not wired into every production API path yet. Existing `CommandPipeline` remains compatible, while new code can adopt the richer runtime model without changing UI behavior or starting agents automatically.
