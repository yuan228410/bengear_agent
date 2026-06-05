# 日志设计

## 目标

日志模块将日志前端和后端工作分离：

- 前端：从热路径轻量级收集日志
- 后端：异步格式化和输出到 sink

这确保请求和工具执行路径在常见情况下不会被文件或网络输出阻塞。

## 组件

```text
LogManager
  └─ Logger
      ├─ 异步队列
      ├─ 工作线程
      └─ sinks
          ├─ StdoutSink
          ├─ FileSink
          └─ TcpServerSink
```

## 日志级别

支持的级别：

- `trace`
- `debug`
- `info`
- `warn`
- `error`
- `critical`
- `off`

## 日志格式 API

BenGear 提供格式化风格的日志接口：

```cpp
log::info_fmt("LLM request: model={}, provider={}", settings.model, provider_name);
log::error_fmt("Chat failed: {}", e.what());
log::debug_fmt("Processing message: role={}, content_length={}", message.role, message.content.size());
log::warn_fmt("tool blocked by role filter: name={}", tool_name);
```

应避免字符串拼接，使用格式化风格：

```cpp
// ❌ 避免
log::info("Processing item " + std::to_string(index));

// ✅ 推荐
log::info_fmt("Processing item {}", index);
```

## Sinks

### StdoutSink

将格式化的日志写入标准输出。

### FileSink

将格式化的日志写入文件。这是默认输出模式。

特性：

- **进程隔离**：默认文件名包含日期和 PID（`bengear_YYYYMMDD_PID.log`），防止多个实例的日志交错
- **基于大小的轮转**：当文件超过 `max_file_size_mb`（默认 10MB）时，轮转为 `.1.log`、`.2.log` 等
- **轮转限制**：超过 `max_rotated_files`（默认 5）的最旧轮转文件会自动删除

轮转示例：

```text
bengear_20260604_12345.log      ← 当前（正在写入）
bengear_20260604_12345.1.log    ← 最近轮转
bengear_20260604_12345.2.log    ← 较旧
...
bengear_20260604_12345.5.log    ← 最旧（下次轮转会删除）
```

### TcpServerSink

在配置的端口上运行 TCP 服务器。日志消费者（聚合器、监控器、`nc`）连接以接收格式化日志行的实时流。当没有客户端连接时，开销可忽略不计。如果端口不可用，sink 会优雅降级（打印警告，禁用网络日志）而不阻塞其他 sink。

配置：

```json
"log": {
    "output": "file,network",
    "network_host": "127.0.0.1",
    "network_port": "9000"
}
```

- `network_host`：监听地址（`127.0.0.1` 仅本地，`0.0.0.0` 所有接口）
- `network_port`：监听端口

使用：

```bash
# 启用网络日志启动 bengear
./bengear

# 从另一个终端连接以实时查看日志
nc 127.0.0.1 9000
```

## 性能

日志器使用：

- 异步队列
- 后端格式化
- 缓存时间戳格式化
- 原子全局日志器访问
- 有界队列和丢弃计数

基准测试位于 `benchmarks/benchmark_main.cpp`。

## 配置

示例：

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

## 条件日志

对于计算成本高的日志内容：

```cpp
if (log::is_debug_enabled()) {
    log::debug_fmt("Detailed info: {}", expensive_to_compute());
}
```

对于高频路径，避免逐项日志：

```cpp
if (counter % 1000 == 0) {
    log::info_fmt("Processed {} items", counter);
}
```

## 扩展指南

添加 sink：

1. 实现 `log::Sink`
2. 将输出特定的逻辑保留在该 sink 中
3. 在 `log::make_logger` 配置逻辑中注册 sink
4. 如果相关，添加专注的测试或基准测试
