# 网络设计

## 当前层级

BenGear 有两个网络层：

1. `bengear_net`：原生 socket/event-loop/TCP 基础层
2. `HttpClient`：高层 HTTP 请求接口，供 LLM 客户端和 HTTP 工具使用

## 原生网络基础

`bengear_net` 提供：

- `NetworkRuntime`：平台运行时初始化（如 Winsock）
- `Socket`：RAII socket 句柄
- `connect_tcp_non_blocking`：非阻塞 TCP 连接
- `tcp_listen` / `tcp_accept`：TCP 服务器原语（bind、listen、accept）
- `udp_bind` / `udp_send_to`：UDP 服务器原语
- `EventLoop`：epoll/kqueue/IOCP 风格的事件循环抽象
- `TcpStream`：基于协程的读/写助手
- `TimerAwaiter`：协程睡眠支持，用于非阻塞重试退避
- `send_tcp_message`：简单的同步 TCP 发送

平台特定的 socket 代码应保留在 `net` 实现文件中。

## HTTP 客户端边界

`HttpClient` 仅暴露异步 API（通过 `sync_wait` 桥接到同步调用）：

- `get_async(loop, url, headers)` — 异步 GET（通过 `sync_wait` 同步调用）
- `post_json_async(loop, url, body, headers)` — 异步 POST JSON
- `post_json_stream_async(loop, url, body, headers, on_chunk)` — 异步流式 POST

所有方法均为异步协程，通过 `sync_wait` 桥接到同步调用：

- `get_async(loop, url, headers)`
- `post_json_async(loop, url, body, headers)`
- `post_json_stream_async(loop, url, body, headers, on_chunk)`

提供商客户端依赖此边界，而不是底层传输细节。

## 流式需求

LLM 流式要求 HTTP 层将响应字节增量传递给 `on_chunk`。此层之上的 SSE 解析器然后提取文本、思考增量和增量工具调用。

当解析器检测到停止条件（例如 `finish_reason: tool_calls`）时，它从回调返回 `false` 以停止读取。`HttpResponse` 在 `callback_stopped` 中记录这一点：

```cpp
struct HttpResponse {
    int status = 0;
    std::string body;
    container::Map<container::String, std::string> headers;
    bool callback_stopped = false;  // 解析器提前停止
};
```

这表示连接不应被复用，因为流未被完全消费。
这表示流未被完全消费。当 `callback_stopped` 且服务端允许 keep-alive 时，`HttpClient` 会调用 `drain_chunked_body()` 消费剩余 chunked 数据直到终止符 `0\r\n\r\n`，使连接可以复用。

## 原生 HTTP 实现

`HttpClient` 直接使用原生 socket 传输，而不是调用外部网络工具。

实现的职责：

1. URL 解析器（scheme、host、port、path、query）
2. HTTP/1.1 请求构建器
3. 请求头解析器
4. 基于 `EventLoop` / `TcpStream` 的协程响应体读取器
5. 基于协程的分块传输解码
6. 接收字节时的增量流式回调
7. 通过 OpenSSL 的协程驱动 TLS 握手/读/写（用于 `https`）
7. 通过 TlsEngine 抽象接口的协程驱动 TLS 握手/读/写（用于 `https`）

## TLS 抽象层

大多数生产 LLM 端点需要 HTTPS。BenGear 通过 `TlsEngine` 抽象接口支持多后端 TLS，将 TLS 细节隔离在网络层内部。

### 后端选择

通过 CMake 选项 `TLS_BACKEND` 选择 TLS 后端：

| 后端 | CMake 值 | 平台 | 依赖 |
|------|---------|------|------|
| MbedTLS（默认） | `mbedtls` | macOS/Linux | vendor（`third_party/mbedtls/`） |
| OpenSSL | `openssl` | 全平台 | 系统 OpenSSL |
| Schannel | `schannel` | Windows | 系统库（secur32/crypt32） |
| 无 TLS | `none` | — | 无 |

```bash
# 使用 MbedTLS（默认）
cmake -DTLS_BACKEND=mbedtls ...
# 使用系统 OpenSSL
cmake -DTLS_BACKEND=openssl ...
# Windows 原生 Schannel
cmake -DTLS_BACKEND=schannel ...
# 禁用 TLS
cmake -DTLS_BACKEND=none ...
```

### TlsEngine 接口

```cpp
class TlsEngine {
public:
    virtual ~TlsEngine() = default;
    class Session {
    public:
        virtual Task<void> handshake(EventLoop& loop, socket_handle fd,
                                     std::string_view host, const TlsConfig& config) = 0;
        virtual Task<void> write_all(EventLoop& loop, std::string_view data) = 0;
        virtual Task<std::size_t> read_some(EventLoop& loop, char* buf, std::size_t size) = 0;
        virtual void* native_handle() noexcept = 0;
        virtual void shutdown() noexcept = 0;
        virtual bool is_connected() const noexcept = 0;
    };
    virtual std::unique_ptr<Session> create_session() = 0;
    virtual void initialize() = 0;
    virtual const char* name() const noexcept = 0;
};
```

全局实例通过 `global_tls_engine()` 获取（延迟初始化），可通过 `set_global_tls_engine()` 替换。

### TlsConfig

```cpp
struct TlsConfig {
    bool verify_peer = true;          // 是否验证服务端证书
    bool enable_sni = true;           // 是否发送 SNI
    std::string ca_cert_path;         // 自定义 CA 路径
    std::string client_cert_path;     // 客户端证书路径（双向 TLS）
    std::string client_key_path;      // 客户端私钥路径
    int min_protocol_version = 0;     // 0=默认(TLS 1.2+), 12=TLS 1.2, 13=TLS 1.3
};
```

### TLS 连接复用

连接池支持 TLS 连接复用，避免每次请求都进行 TCP + TLS 握手：

- 首次请求：`tls_session=nullptr`，建立新 TCP 连接 + TLS 握手（~2-6s）
- 后续请求：`tls_session!=nullptr`，直接复用池中 TLS 连接（~1s）

**关键实现**：
- `from_pooled_stream()`：从池中取出 TLS 连接时，直接 move `TlsEngine::Session`，类型安全，无需手动管理 SSL 对象
- `drain_chunked_body()`：流式请求回调提前停止后，消费剩余 chunked body 使连接可复用
- 空闲超时淘汰：`acquire` 取出连接时检查空闲时间，超过 `idle_timeout` 直接丢弃（避免代理静默断开导致 `tls write failed`）

**重试机制**：如果复用的 TLS 连接写入失败，自动 retry 新建连接，用户无感知。

```text
HttpClient
  └─ Transport
     ├─ raw TCP socket path for http
     └─ TlsEngine::Session TLS path for https
```

提供商客户端不直接依赖任何 TLS 后端实现。

## 非目标

网络层不应知道：

- OpenAI
- Anthropic
- Agent 工具调用
- CLI 终端渲染
- 模型配置语义

这些关注点属于更高层。

## 性能注意事项

重要的性能考虑：

- 避免将流式响应写入临时文件
- 一次性解析响应头
- 增量解码分块传输
- 避免热流式路径上不必要的字符串拷贝
- 为 HTTP 和 HTTPS 传输复用事件循环原语
- 当多个 Agent、工具或 LLM 请求共享一个事件循环时，优先使用 `*_async` API
- 保持回调轻量

## 连接池

### 线程安全

`ConnectionPool` 内部使用 `std::shared_mutex` 进行读写锁定：

- `acquire`（获取连接）使用独占锁，修改池状态
- `release`（归还连接）使用独占锁，修改池状态
- `size`、`cleanup_idle` 和其他查询使用共享锁，允许并发读取

### 对象池集成

`ConnectionPool` 与 `ObjectPool<PooledConnection>` 集成，以减少高并发连接抖动时的堆分配开销：

- `ConnectionPoolConfig::enable_object_pool`（默认 `true`）控制对象池是否激活
- 启用时，`PooledConnection` 对象通过 `object_pool_->create()` 分配，通过 `object_pool_->destroy()` 归还，复用由 `FixedSizePool` 支持的空闲列表中的内存
- 禁用时，回退到 `new`/`delete`
- `object_pool_stats()` 暴露池利用率指标（`total_created`、`total_destroyed`、`pool_size`、`active_count`）
- 通过 `config.json` 配置：`connection_pool.enable_object_pool`

### 连接池预热

`ConnectionPool` 提供 `warmup` 协程，用于在启动时预创建连接，以消除首次请求的 TCP + TLS 握手延迟：

```cpp
// 为 api.openai.com 预热 3 个 HTTPS 连接
co_await pool.warmup(loop, /*tls=*/true, "api.openai.com", "443", 3);
```

参数：
- `loop`：事件循环引用
- `tls`：是否使用 TLS（https 端点为 true）
- `host` / `port`：目标主机和端口
- `count`：预创建的连接数

预创建的连接进入空闲池。后续 `acquire` 调用直接复用它们，无需等待 TCP + TLS 握手。

### 配置

```json
{
  "connection_pool": {
    "max_connections_per_host": 10,
    "idle_timeout_seconds": 30,
    "connect_timeout_seconds": 10,
    "response_timeout_seconds": 60,
    "enable_keep_alive": true,
    "enable_object_pool": true
  }
}
```

## 响应超时保护

当 LLM 服务端在建立连接后不返回数据时，`read_some` 会通过 `co_await loop_->wait_read()` 无限等待。为防止永久挂起，`HttpClient::send_with_transport` 注册 `close_after` 读空闲超时：

```cpp
// 发送请求前：设置读空闲超时
loop->close_after(fd, response_timeout);

// 每次成功读到数据后：刷新超时（重新计时）
refresh_timeout();  // cancel_close + close_after

// 正常完成后：取消超时
loop->cancel_close(fd);
```

**读空闲超时 vs 整体超时**：

超时按"两次数据到达之间的最大间隔"计算，而非从请求发出到完成的整体时间。每次 `read_some` 成功读取数据后，自动刷新超时计时器。这样 LLM 流式长响应（如生成 5000 字文章）不会被误杀，只要服务端持续发送数据就不会超时。

**超时触发流程**：

1. `EventLoop::run_once` Phase 5 检测到超时（两次数据间超时），关闭 fd
2. 扫描 `pending` 中挂起在该 fd 上的 I/O 操作，标记 `cancelled = true`
3. 在锁外恢复挂起的协程
4. `IoAwaiter::await_resume` 检测 `cancelled`，抛出 `ResponseTimeoutError`
5. 异常传播至 `request_once`，`dynamic_cast` 检测到 `ResponseTimeoutError` 后直接传播不重试

**配置**：

```json
{
  "connection_pool": {
    "response_timeout_seconds": 60
  }
}
```

默认 60 秒（读空闲超时）。可通过配置调整，设为 0 禁用超时（不推荐）。
