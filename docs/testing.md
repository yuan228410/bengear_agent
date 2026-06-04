# Testing and Benchmarking

## Unit Tests

Tests live in `tests/test_main.cpp` and are built as `bengear_tests`.

Run:

```bash
ctest --test-dir build --output-on-failure
```

Current coverage includes:

- string utilities
- JSON escaping and extraction
- OpenAI request shape and headers
- Anthropic request shape and headers
- stream parsing for text and thinking deltas
- endpoint URL completion
- config loading
- LLM retry behavior
- async LLM retry behavior with timer awaiter
- built-in tool parsing and file tools
- coroutine task behavior
- event loop construction and timer wakeups

## Benchmarks

Benchmarks live in `benchmarks/benchmark_main.cpp` and are built as `bengear_benchmarks`.

Run:

```bash
./build/bengear_benchmarks
```

Current benchmark areas:

- string support utilities
- JSON helpers
- config loading
- coroutine task resume
- event loop polling
- logging disabled/enqueue/file/contention paths

## Build Matrix

Useful local build variants:

```bash
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build-debug
ctest --test-dir build-debug --output-on-failure
```

```bash
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release
cmake --build build-release
./build-release/bengear_benchmarks
```

## Testing Guidelines

When adding code:

- Add focused unit tests near existing coverage.
- Keep tests deterministic and avoid external network calls by default.
- Use benchmarks for hot-path changes such as logging, parsing, and network streaming.
- Do not add new global dependencies unless they are required by production code.

## Manual Verification

Useful manual checks:

```bash
./build/bengear --show-config
./build/bengear --active-model oneapi-deepseek "你好"
./build/bengear --active-model oneapi-claude-sonnet "hello"
./build/bengear --async --stream "hello"
```

When real API keys are involved, avoid saving command output into tracked files.
