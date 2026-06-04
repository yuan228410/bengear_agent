# Logging Design

## Goals

The logging module separates logging frontend and backend work:

- frontend: lightweight log collection from hot paths
- backend: asynchronous formatting and sink output

This keeps request and tool execution paths from blocking on file or network output in common cases.

## Components

```text
LogManager
 └─ Logger
     ├─ async queue
     ├─ worker thread
     └─ sinks
         ├─ StdoutSink
         ├─ FileSink
         └─ TcpServerSink
```

## Levels

Supported levels:

- `trace`
- `debug`
- `info`
- `warn`
- `error`
- `critical`
- `off`

## Sinks

### StdoutSink

Writes formatted logs to stdout.

### FileSink

Writes formatted logs to a file. This is the default output mode.

Features:

- **Process isolation**: default filename includes date and PID (`bengear_YYYYMMDD_PID.log`), preventing log interleaving from multiple instances
- **Size-based rotation**: when the file exceeds `max_file_size_mb` (default 10MB), it is rotated to `.1.log`, `.2.log`, etc.
- **Rotation limit**: oldest rotated files beyond `max_rotated_files` (default 5) are automatically deleted

Rotation example:

```text
bengear_20260604_12345.log      ← current (being written)
bengear_20260604_12345.1.log    ← most recent rotation
bengear_20260604_12345.2.log    ← older
...
bengear_20260604_12345.5.log    ← oldest (next rotation deletes this)
```

### TcpServerSink

Runs a TCP server on the configured port. Log consumers (aggregators, monitors, `nc`) connect to receive a live stream of formatted log lines. When no client is connected, the overhead is negligible. If the port is unavailable, the sink degrades gracefully (prints a warning, disables network logging) without blocking other sinks.

Configuration:

```json
"log": {
  "output": "file,network",
  "network_host": "127.0.0.1",
  "network_port": "9000"
}
```

- `network_host`: listen address (`127.0.0.1` for local only, `0.0.0.0` for all interfaces)
- `network_port`: listen port

Usage:

```bash
# Start bengear with network logging enabled
./bengear

# Connect from another terminal to watch logs live
nc 127.0.0.1 9000
```

## Performance

The logger uses:

- async queue
- backend formatting
- cached timestamp formatting
- atomic global logger access
- bounded queue with drop counting

Benchmarks are available in `benchmarks/benchmark_main.cpp`.

## Configuration

Example:

```json
"log": {
  "level": "info",
  "output": "file,stdout",
  "file": "",
  "network_host": "127.0.0.1",
  "network_port": "9000",
  "max_file_size_mb": 10,
  "max_rotated_files": 5
}
```

## Extension Guidelines

To add a sink:

1. Implement `log::Sink`.
2. Keep output-specific logic inside that sink.
3. Register the sink in `log::make_logger` configuration logic.
4. Add a focused test or benchmark if relevant.
