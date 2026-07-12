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

## Execution trace persistence and inspection

Runtime execution is persisted separately from audit so that execution evidence can be queried without scanning human-facing audit entries.

- Audit events remain in `audit/events.jsonl`.
- Runtime executions are written to `runtime/executions.jsonl`.
- Each persisted execution stores `execution_id`, workspace/session/user identity, command action, runtime operation, final status, serialized execution result, and `audit_event_id` when the paired audit append succeeds.

Inspection APIs are read-only:

```text
GET /api/runtime/executions
GET /api/runtime/executions/:execution_id
GET /api/runtime/executions/:execution_id/trace
```

`/api/runtime/executions` supports filtering by workspace, session, action, status, capability, and limit. The trace endpoint returns the serialized guarded sequence for debugging permission/checkpoint/execution/audit behavior without changing mutation semantics.

## Runtime-aware diagnostics and repair

Diagnostic context accepts runtime execution evidence either by `runtime_execution_id` or by an inline `runtime_execution` object. When an id is supplied, the context service reads the matching record from `runtime/executions.jsonl` and includes it in the read-only repair context output.

Diagnostic repair uses the first failed runtime trace step to avoid unsafe or misleading code-repair plans:

- `authorize` failure → permission remediation; no source candidate files are proposed.
- `checkpoint` failure → workspace/checkpoint remediation before retrying mutation.
- `audit` failure → observability/persistence remediation.
- `validate` failure → request metadata remediation.
- `execute` failure → normal build/test diagnostic repair continues, with runtime evidence attached to the summary.

This keeps repair planning aligned with the runtime boundary: only handler execution failures are treated as code/test repair candidates.

## Core / Runtime / UI boundary (phase 1)

Phase 1 makes the execution boundary explicit and UI-independent:

- **Core (`src/core/runtime_boundary.hpp`)** owns stable data contracts only: request/workspace refs, runtime operation/boundary, mutation scope, runtime status, and `RuntimeEvent`. It must not include CLI, Server, Agent, Workspace, Patch, Git, or other concrete adapters.
- **Runtime/Application (`src/application/runtime_execution.hpp`)** owns orchestration. `RuntimeExecutionKernel` produces an `ExecutionPlan`, emits structured `RuntimeEvent` values, runs validate/authorize/checkpoint/execute/audit hooks, and returns an `ExecutionResult` with trace evidence.
- **UI adapters (CLI/Web/Server)** consume the structured state. They may render, serialize, filter, or persist runtime events/results, but must not own permission/checkpoint/audit sequencing or mutate Core DTOs into UI-specific shapes.

A concrete migrated path is CLI single request (`bengear --prompt ...`): `run_single_request_session` wraps the existing agent call in `RuntimeExecutionKernel` with action `cli.single_request`. The CLI uses `cli::RuntimePresenter` to render `RuntimeEvent`/`ExecutionResult` from the runtime boundary rather than coupling terminal output to execution state. The LLM token stream still flows through the existing `agent::runtime::RuntimeEventSink` adapter; the request lifecycle now has a separate structured runtime trace.

Design rules for new work:

1. Add new execution state to Core DTOs first if it must cross Runtime/UI boundaries.
2. Put sequencing and state production in Runtime/Application hooks or services.
3. Keep CLI/Web/Server as presenters/adapters; they should depend on Core/Application data, not the other way around.
4. Persist or expose `ExecutionResult`/`RuntimeEvent` JSON for diagnostics instead of scraping UI text.
