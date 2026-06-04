# Networking Design

## Current Layers

BenGear has two networking layers:

1. `bengear_net`: native socket/event-loop/TCP foundation.
2. `HttpClient`: higher-level HTTP request interface used by LLM clients and HTTP tools.

## Native Network Foundation

`bengear_net` provides:

- `NetworkRuntime`: platform runtime initialization such as Winsock.
- `Socket`: RAII socket handle.
- `connect_tcp_non_blocking`: non-blocking TCP connect.
- `tcp_listen` / `tcp_accept`: TCP server primitives (bind, listen, accept).
- `udp_bind` / `udp_send_to`: UDP server primitives.
- `EventLoop`: epoll/kqueue/IOCP-style event loop abstraction.
- `TcpStream`: coroutine-based read/write helpers.
- `TimerAwaiter`: coroutine sleep support for non-blocking retry backoff.
- `send_tcp_message`: simple synchronous TCP send.

Platform-specific socket code should remain inside `net` implementation files.

## HTTP Client Boundary

`HttpClient` exposes synchronous compatibility APIs:

- `get(url, headers)`
- `post_json(url, body, headers)`
- `post_json_stream(url, body, headers, on_chunk)`

It also exposes coroutine APIs for high-concurrency callers:

- `get_async(loop, url, headers)`
- `post_json_async(loop, url, body, headers)`
- `post_json_stream_async(loop, url, body, headers, on_chunk)`

Provider clients depend on this boundary rather than lower-level transport details.

## Streaming Requirements

LLM streaming requires the HTTP layer to deliver response bytes incrementally to `on_chunk`. The SSE parser above this layer then extracts text and thinking deltas.

## Native HTTP Implementation

`HttpClient` uses native socket transport directly rather than shelling out to external network tools.

Implemented responsibilities:

1. URL parser for scheme, host, port, path, and query.
2. HTTP/1.1 request builder.
3. Header parser.
4. Coroutine-based response body reader on `EventLoop` / `TcpStream`.
5. Coroutine-based chunked transfer decoding.
6. Incremental streaming callback while bytes are received.
7. Coroutine-driven TLS handshake/read/write for `https` through OpenSSL.

## TLS Dependency

Most production LLM endpoints require HTTPS. BenGear uses OpenSSL for TLS while keeping TLS details isolated inside the network layer.

```text
HttpClient
 └─ Transport
     ├─ raw TCP socket path for http
     └─ OpenSSL TLS path for https
```

Provider clients do not depend on OpenSSL directly.

## Non-Goals

The network layer should not know about:

- OpenAI
- Anthropic
- Agent tool calls
- CLI terminal rendering
- model configuration semantics

Those concerns belong to higher layers.

## Performance Notes

Important performance considerations:

- Avoid writing streaming responses to temporary files.
- Parse response headers once.
- Decode chunked transfer incrementally.
- Avoid unnecessary string copies on hot streaming paths.
- Reuse event-loop primitives for HTTP and HTTPS transport.
- Prefer `*_async` APIs when multiple Agents, tools, or LLM requests share one event loop.
- Keep callbacks lightweight.

## Connection Pool Object Reuse

`ConnectionPool` integrates with `ObjectPool<PooledConnection>` to reduce heap allocation overhead on high-concurrency connection churn:

- `ConnectionPoolConfig::enable_object_pool` (default `true`) controls whether the object pool is active.
- When enabled, `PooledConnection` objects are allocated via `object_pool_->create()` and returned via `object_pool_->destroy()`, reusing memory from a free list backed by `FixedSizePool`.
- When disabled, falls back to `new`/`delete`.
- `object_pool_stats()` exposes pool utilization metrics (`total_created`, `total_destroyed`, `pool_size`, `active_count`).
- Configurable via `config.json`: `connection_pool.enable_object_pool`.
