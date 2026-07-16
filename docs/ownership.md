# Ownership and Lifecycle Rules

BenGear has several long-lived resource graphs (`Runtime`, `WorkflowEngine`,
`SubAgentRuntime`, `ToolRegistry`, `IoContext`). These objects are deliberately shared
across agents and sessions, so ownership rules must stay explicit to avoid leaks,
use-after-free, and hidden shutdown hangs.

## Root ownership

`agent::Runtime` is the root owner for per-user/per-workspace resources.
It may strongly own services such as:

- `workflow::WorkflowEngine`
- `workflow::WorkflowTemplateLibrary`
- `agent::runtime::SubAgentRuntime`
- `llm::ToolRegistry`
- memory/workspace/MCP/permission/checkpoint services
- `net::IoContext` instances

Services owned by `Runtime` must not strongly own `Runtime` back.
If they need to call back into the root, store a `std::weak_ptr<Runtime>`
or a non-owning pointer whose lifetime is guarded by an external owner.

## Forbidden cycles

Avoid these shapes:

```text
Runtime
  -> WorkflowEngine
    -> WorkflowResources
      -> shared_ptr<Runtime>
```

```text
Runtime
  -> ToolRegistry
    -> ToolExecutor closure
      -> SubAgentRuntime
        -> Runtime
```

Both keep the root resource tree alive after the owning `Agent` is destroyed.

## ToolRegistry closure rules

`ToolRegistry` is owned by `Runtime`, and tool executors are stored as
`std::function` closures. Therefore:

- Do not capture `std::shared_ptr<Runtime>` in tool executors.
- Do not capture `std::shared_ptr<T>` for objects already strongly owned by
  `Runtime` when the executor is stored in `Runtime::tools_`.
- Prefer `std::weak_ptr<T>` and `lock()` at call time.
- Return a structured error if the weak pointer has expired.
- Validate every weakly accessed resource before dereferencing it.

Good pattern:

```cpp
std::weak_ptr<Service> weak_service = service;
registry.register_tool("tool", "desc", params,
    [weak_service](const Json& args) -> std::string {
        auto service = weak_service.lock();
        if (!service) return R"({"success":false,"error":"service expired"})";
        return service->run(args);
    });
```

## WorkflowResources rules

`workflow::WorkflowResources` is a non-owning binding object used by
`WorkflowEngine` and workflow tasks.

- Its raw pointers (`tools`, `settings`, `wf_context`) point into `Runtime`.
- `WorkflowEngine` is owned by `Runtime`, so `WorkflowResources` stored in
  the engine must not keep a strong `Runtime` reference.
- Chat/task callbacks should capture `std::weak_ptr<Runtime>` and lock it
  only for the duration of the operation.
- If a workflow resource callback runs after root destruction, it must return a
  normal error result rather than dereferencing stale pointers.

## SubAgentRuntime rules

`SubAgentRuntime` is owned by `Runtime` and must weakly refer back to it.

- Store `std::weak_ptr<Runtime>` in `SubAgentRuntime`.
- `SubAgentRuntime::resources()` may return `nullptr`; callers must check.
- Sub-agent tool closures should weakly capture `SubAgentRuntime`.
- Filtered tool registries for sub-agents must continue excluding recursive
  delegation tools (`delegate_task`, `delegate_tasks`).

## EventLoop / sync_wait rule

Do not call `sync_wait(loop, ...)` from the same thread that owns `loop`. This can
self-deadlock. If a new call site is added:

1. Confirm the caller thread is not the target loop thread, or
2. Use coroutine composition (`co_await`) instead of blocking `sync_wait`, and
3. Add a regression test for the execution path.

## Regression tests

Lifecycle-sensitive changes should run:

```bash
./build-dev/bengear_tests --filter LifecycleTest.*
```

ASAN leak checks are recommended for changes touching ownership, tool closures,
workflow resources, or event-loop shutdown:

```bash
ASAN_OPTIONS=detect_leaks=1 ./build-asan/bengear_tests --filter LifecycleTest.*
```

New lifecycle tests should use `std::weak_ptr` probes around object scopes and
assert that resources expire after the owner leaves scope.
