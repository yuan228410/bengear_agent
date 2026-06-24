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
    -> bengear_compress
    -> bengear_tls
      -> bengear_net

bengear_acp_core
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
