# LLM Protocol Design

## Provider Boundary

`ProviderClient` is the protocol dispatch boundary. It checks the configured provider and delegates to a protocol-specific client:

- `OpenAiClient`
- `AnthropicClient`

The agent only talks to `ProviderClient` and does not know provider request formats.

## OpenAI Chat Completions

OpenAI-style requests use:

- endpoint: `/v1/chat/completions`
- auth header: `Authorization: Bearer <api_key>`
- body fields: `model`, `temperature`, `max_tokens`, `messages`
- stream field: `"stream": true`

System prompts are represented as a `system` role message.

Streaming output is parsed from SSE events with `choices[].delta.content`. Reasoning output is parsed from compatible fields such as `reasoning_content` or `thinking` when present.

## Anthropic Messages

Anthropic-style requests use:

- endpoint: `/v1/messages`
- auth header: `x-api-key: <api_key>`
- version header: `anthropic-version: <version>`（默认 `2023-06-01`，可通过模型配置的 `anthropic_api_version` 字段覆盖）
- body fields: `model`, `max_tokens`, `temperature`, `system`, `messages`
- stream field: `"stream": true`

System prompts use Anthropic's top-level `system` field.

Streaming output is parsed from SSE `content_block_delta` events. Text deltas are read from `text`; thinking deltas are read from `thinking` or `thinking_delta` when present.

## Streaming Model

The streaming API uses `StreamHandlers`:

```cpp
struct StreamHandlers {
    StreamTokenHandler on_token;
    StreamThinkingHandler on_thinking;
};
```

This keeps normal response tokens and thinking/reasoning tokens separate.

## Retry Model

Provider clients wrap LLM requests with `with_retry(...)`.

Retry policy is configured by `llm_request_retry`. The retry helper is provider-agnostic and only requires the result type to expose a `status` field.

## Extension Guidelines

To add a new provider:

1. Add a provider enum value.
2. Add a protocol-specific client.
3. Add request body and header builders.
4. Add a stream parser if streaming is supported.
5. Extend `ProviderClient` dispatch.
6. Add tests for body shape, headers, endpoint completion, and stream parsing.

Do not add provider-specific protocol branches in `Agent`.
