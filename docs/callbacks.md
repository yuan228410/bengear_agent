# Callback Design

## Purpose

Callbacks expose agent runtime events without coupling the agent to a specific frontend.

Current terminal rendering is only one implementation. The same interface can be reused by a GUI, web server, tracing backend, or test harness.

## Interface

`AgentCallbacks` provides:

```cpp
virtual void on_thinking(std::string_view token);
virtual void on_token(std::string_view token);
virtual void on_tool_call(const ToolCallRequest& call);
virtual void on_tool_result(const ToolCallResult& result);
```

## Event Types

### Thinking

`on_thinking` receives reasoning/thinking deltas parsed from provider streaming responses.

Provider-specific parsers normalize fields into this callback:

- OpenAI-compatible: `reasoning_content`, `thinking`
- Anthropic-compatible: `thinking`, `thinking_delta`

### Token

`on_token` receives final answer text tokens.

### Tool Call

`on_tool_call` fires before local tool execution.

### Tool Result

`on_tool_result` fires after tool execution with status and output metadata.

## Terminal Implementation

The CLI currently prints:

```text
[thinking] ... [/thinking]
[tool call] read_file path=/tmp/example.txt
[tool result] ok bytes=123
```

Final answer tokens are printed to stdout.

## Extension Guidelines

To add a new frontend:

1. Subclass `AgentCallbacks`.
2. Keep rendering or serialization logic in that subclass.
3. Pass it into `Agent::run` or `Agent::run_stream`.
4. Avoid adding frontend-specific branches to `Agent`.

## Design Principle

Callbacks are event delivery only. They should not perform provider protocol parsing, tool execution, or retry decisions.
