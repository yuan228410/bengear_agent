# Module Split Plan

This document tracks the staged split of the current coarse `bengear_workflow`
target into smaller libraries. The goal is to reduce incremental build cost,
make ownership boundaries visible, and make unit tests link only what they need.

## Current problem

`bengear_workflow` currently contains workflow, agent, memory, tools, server,
patch/git/checkpoint services, diagnostics, permission, audit, and MCP code.
This has a few costs:

- touching common headers rebuilds most of the project;
- server code is linked into CLI/test paths that only need agent/workflow logic;
- ownership relationships are harder to inspect because unrelated modules share
  one binary boundary;
- focused tests cannot link small slices of the system.

## Target library graph

The intended end-state is staged, not a single large rewrite:

```text
bengear_json
  -> bengear_base
    -> bengear_core          (UI-free request/runtime boundary model)
    -> bengear_compress
    -> bengear_tls
      -> bengear_net

bengear_acp_core
bengear_core            (request/workspace/runtime-operation model; no UI/runtime deps)
bengear_config          (settings/loader if split from base later)
bengear_llm             (provider clients, adapters, retry, failover)
bengear_tool            (registry, manager, tool types)
bengear_memory          (store, episode, context, compactor, pruner)
bengear_workspace       (manager, session, history DB/exporter)
bengear_application     (command pipeline/governance/use cases)
bengear_services        (patch/git/checkpoint/test-loop/repo-map/code-intel/etc.)
bengear_agent           (Agent, SharedResources, SubAgentRuntime)
bengear_workflow        (DAG workflow engine/tasks/templates only)
bengear_server          (HTTP/WS/REST/session pool/callback bridge)
bengear_cli_render
bengear_cli_repl
bengear                 (main executable)
```

Directionally:

- `bengear_agent` may depend on workflow services, but server must not be inside
  the workflow library.
- `bengear_server` depends on agent/workflow/workspace/application services.
- `bengear_cli_repl` depends on agent and render, not directly on server.
- Tool implementations should move from large header-only registration files to
  `.cpp` files with narrow public declarations.

## Staging

### Stage 1: document and test ownership

Status: done.

- Add ownership/lifecycle rules.
- Add lifecycle regression tests for `SharedResources`, `WorkflowResources`, and
  `SubAgentRuntime`.
- Add low-memory presets so target splits can be measured reliably.

### Stage 1.5: establish core/runtime/UI boundary

Status: done.

- Add `bengear_core` for UI-free request, workspace, runtime operation, tool,
  permission, patch, diff, git, checkpoint, and repo-map references.
- Make application request context use the core request model instead of owning a
  parallel shape.
- Add architecture guardrails proving core headers do not include application,
  agent, CLI, server, workflow, workspace, or concrete service headers.

### Stage 1.6: unify mutating runtime command governance

Status: done.

- Map application command descriptors into core `RuntimeOperation` values with
  explicit capability and mutation scope.
- Build a reusable `RuntimeBoundary` for command governance so permission,
  checkpoint, execution, and audit observe the same operation shape.
- Carry core `PermissionGateRef` and runtime operation details into permission
  arguments, and include the full runtime boundary in audit records.
- Cover Patch/Diff/Git/Permission/Checkpoint/Test Loop command families with
  architecture tests and full regression coverage.

### Stage 1.7: request-scoped indexed code intelligence layer

Status: done.

- Add `code_intel::CodeIntelligenceIndex` as a request-scoped facade over
  repo-map and code-intel services.
- Route overview, path explanation, file search, workspace symbols,
  document symbols, definition, and references through one shared
  `RequestIndexSession` when callers use the same index options.
- Preserve the UI-free indexed provider boundary: callers compose repository
  structure and code navigation without binding to web or CLI adapters.
- Cover cross-query reuse with regression tests proving repo-map and code-intel
  queries can share a single workspace index build.

### Stage 1.8: runtime-aware web workspace snapshot

Status: done.

- Add `/api/workbench/snapshot` as a single Web Workbench entry point.
- Compose repository overview, file/path explanation, document symbols,
  workspace symbols, definition, references, and recent audit events into one
  response for Web clients.
- Route code navigation through `CodeIntelligenceIndex` so repo-map and
  code-intel reads can share a request-scoped workspace index.
- Keep workspace selection, username, audit lookup, and index options on the
  server-side composition boundary instead of scattering them across Web calls.
- Cover route parsing and composition-level snapshot behavior with regression
  tests.

### Stage 1.9: web workbench frontend panel

Status: done.

- Add a right-panel Workbench tab that calls `POST /api/workbench/snapshot`
  instead of making separate Repo Map, Code Intelligence, and Audit requests.
- Show one runtime-aware snapshot: repository summary, important/searched files,
  workspace/document symbols, definition/reference results, request-scoped index
  state, and recent audit events.
- Keep the existing specialized Repo Map / Code Intelligence / Audit tabs, but
  make Workbench the productized entry point for the integrated development
  flow.
- Type the snapshot response in the Web protocol layer and centralize request
  state in `use-workbench`.

### Stage 1.10: workbench source context

Status: done.

- Extend `POST /api/workbench/snapshot` with bounded `source_context` for the
  selected path and focus line, so the workbench can show readable code context
  next to repo-map and code-intel results.
- Keep source reads inside the resolved workspace root and return a structured
  `workspace_escape` error for paths outside the project.
- Add frontend typing and rendering for source context, including primary-line
  highlighting and configurable context window size.
- Cover normal source context and workspace-escape behavior in composition tests.

### Stage 1.11: workbench navigation context pack

Status: done.

- Extend workbench snapshots with `navigation_contexts`, a bounded source-context
  pack for definition and reference locations returned by Code Intelligence.
- Keep navigation context reads under the same workspace-root safety boundary as
  the selected file source context.
- Render definition/reference context snippets in the Web Workbench so users can
  inspect navigation targets without leaving the integrated panel.
- Add composition test coverage that verifies definition and reference context
  packs are present with the shared snapshot.

### Stage 1.12: workbench change context

Status: done.

- Extend workbench snapshots with `change_context`, combining Git status, selected
  file diff, and Repo Map test suggestions into the same request-scoped view.
- Surface selected-file Git state and unstaged diff in the Web Workbench so code
  inspection and change review live in one integrated panel.
- Keep change context read-only; no restore, commit, or mutation action is added
  to the snapshot path.
- Add composition coverage for modified selected files and diff contents.

### Stage 1.13: workbench quality context

Status: done.

- Extend workbench snapshots with `quality_context`, a read-only bundle that
  accepts diagnostics or raw diagnostic output and returns bounded repair-context
  snippets beside existing test suggestions.
- Reuse the Diagnostic Context service inside the request-scoped composition
  layer, preserving workspace path safety and optional Code Intelligence
  enrichment.
- Render diagnostic snippets in the Web Workbench so quality signals, source
  context, navigation context, and change context are visible in one panel.
- Add composition coverage for diagnostic snippet generation through the
  workbench snapshot API.

### Stage 1.14: workbench action context

Status: done.

- Extend workbench snapshots with `action_context`, a read-only prioritized list
  derived from Source, Navigation, Change, Quality, and Audit context blocks.
- Provide concrete next-step actions such as inspecting diagnostics, reviewing
  selected diffs, running recommended tests, or reading navigation/source
  context without adding mutating behavior to the snapshot API.
- Render action cards in the Web Workbench so the integrated panel becomes a
  guided navigation surface rather than only a data dump.
- Add composition assertions that diagnostics and changed files generate the
  expected highest-priority actions.

### Stage 1.15: workbench verification context

Status: done.

- Extend workbench snapshots with `verification_context`, a read-only quality gate
  bundle that merges Repo Map test suggestions, Test Loop detected commands,
  diagnostic input state, and Git dirty-state into one verification view.
- Preserve the snapshot boundary: commands are suggested and ranked, but never
  executed by the workbench snapshot API.
- Render Verification Context in the Web Workbench with command candidates,
  diagnostic counts, dirty-file state, and suggested next verification steps.
- Add composition assertions that changed files and diagnostic input populate the
  verification context alongside action context.

### Stage 1.16: workbench handoff context

Status: done.

- Extend workbench snapshots with `handoff_context`, a compact read-only summary
  distilled from Source, Change, Quality, Verification, Action, and Audit context.
- Include selected path/query/symbol, status, risk signals, top actions, and the
  recommended verification command so a human or follow-up agent can continue
  from the same state without re-deriving priorities.
- Render Handoff Context in the Web Workbench as the top-level status/risk block
  for review and future delegation flows.
- Add composition assertions for dirty-workspace and diagnostics handoff status.

### Stage 1.17: workbench review context

Status: done.

- Extend workbench snapshots with `review_context`, a read-only reviewer-oriented
  checklist distilled from Handoff, Change, Quality, Verification, and Action
  contexts.
- Include review status, blocker count, focused evidence, and checklist items for
  handoff status, workspace changes, diagnostics, verification command, and next
  actions.
- Render Review Context in the Web Workbench before Handoff so human reviewers
  and future agents can quickly see what still needs attention.
- Add composition assertions for review blockers in dirty-workspace and
  diagnostics scenarios.

### Stage 1.18: workbench dependency context

Status: done.

- Extend workbench snapshots with `dependency_context`, elevating selected-path
  dependencies, dependents, and related tests from path explanation into a
  dedicated read-only context block.
- Attach bounded source snippets for local dependency targets, dependent files,
  and related test files using the same workspace-root safety checks as Source
  Context.
- Render Dependency Context in the Web Workbench so users can inspect the
  selected file's impact neighborhood without extra API calls.
- Add composition assertions that dependent files are exposed with source context.

### Stage 1.19: workbench symbol context

Status: done.

- Extend workbench snapshots with `symbol_context`, a bounded source-context pack
  for document symbols and workspace symbol matches.
- Attach snippets around symbol locations using the same source safety boundary as
  file, navigation, and dependency contexts.
- Render Symbol Context in the Web Workbench so symbols can be inspected inline
  without opening separate code-intelligence panels.
- Add composition assertions that document symbol contexts are produced with
  source snippets.

### Stage 1.20: workbench impact context

Status: done.

- Extend workbench snapshots with `impact_context`, a read-only impact summary
  derived from dependency, symbol, change, and diagnostic contexts.
- Compute impact score and level from dependents, dependencies, related tests,
  symbols, selected diffs, workspace dirtiness, and diagnostics.
- Provide weighted impact factors plus recommended review/verification focus.
- Render Impact Context in the Web Workbench as a compact score, metric chips,
  focus list, and contributing factors.
- Add composition assertions that impact context is present, read-only, scored,
  and carries recommended focus.

### Stage 2: extract low-level stable libraries

Start with modules that have small dependency surfaces:

1. `bengear_tool`: `src/tool/*.cpp` + `include/ben_gear/tool/*`
2. `bengear_memory`: `src/memory/*.cpp` + memory headers
3. `bengear_workspace`: `src/workspace/*.cpp` + workspace headers
4. `bengear_acp_core`: already separate; keep it independent

Each extraction must pass:

```bash
cmake --preset dev
cmake --build --preset dev-tests
./build-dev/bengear_tests
```

### Stage 3: move heavy tool registrations to `.cpp`

Header-only tool registration increases rebuild fanout. Prefer:

```text
include/ben_gear/tools/foo_tools.hpp   declarations only
src/tools/foo_tools.cpp                registration implementation
```

Priority:

1. workflow tools
2. memory/history tools
3. git/checkpoint/patch/test-loop tools
4. diagnostic/repo-map/code-intel tools

### Stage 4: split server from workflow

Create `bengear_server` for:

- `src/server/core/*`
- `src/server/http/*`
- `src/server/ws/*`
- `src/server/api/*`
- `src/server/auth/*`
- `src/server/session/*`
- `src/server/callback/*`
- `src/server/composition/*`

Then make:

```text
bengear_cli_repl -> bengear_agent/bengear_workflow
bengear          -> bengear_cli_repl + bengear_server only if serve mode requires it
```

If CLI serve mode needs server symbols, keep the executable linking both, but do
not force all workflow tests to link server implementation.

## Guardrails

- Keep each extraction as a separate commit.
- Do not change behavior while moving files between targets.
- Run at least the full test binary after each target split.
- Keep low-memory build path working with `cmake --build --preset dev-tests`.
- Avoid introducing new ownership edges while splitting targets; refer to
  `docs/ownership.md`.

## Success metrics

Track these after each stage:

- clean configure with `cmake --preset dev`;
- clean single-thread build with `cmake --build --preset dev-tests`;
- full test pass count;
- incremental rebuild impact for a representative header-only tool edit;
- whether server files still rebuild when editing agent-only code.
