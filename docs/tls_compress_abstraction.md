# TLS 与压缩抽象层设计

## 概述

BenGear 将 TLS 和压缩操作从直接依赖具体库（OpenSSL、zlib）重构为后端无关的抽象接口，实现：

- **编译期后端选择**：通过 CMake 选项切换 TLS/压缩后端
- **运行时后端替换**：通过全局引擎 setter 注入自定义实现
- **类型安全**：消除 `void*` 裸指针，使用 `unique_ptr` 管理 TLS 会话生命周期
- **零上层依赖**：HttpClient、提供商客户端等不直接依赖任何 TLS/压缩后端

---

## TLS 抽象层

### 架构

```
┌──────────────────────────────────────┐
│  HttpClient / ProviderClient         │  ← 上层只依赖 TlsEngine 接口
├──────────────────────────────────────┤
│  TlsEngine (抽象接口)                │  ← handshake / write / read
│  TlsEngine::Session                  │  ← 一次 TLS 连接的抽象
│  TlsConfig                           │  ← 后端无关的配置
├──────────────────────────────────────┤
│  MbedTlsEngine │ OpenSslEngine │ SchannelEngine  │  ← 具体后端
└──────────────────────────────────────┘
```

### TlsEngine 接口

```cpp
class TlsEngine {
public:
    virtual ~TlsEngine() = default;

    class Session {
    public:
        virtual ~Session() = default;
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
    virtual void free_native_handle(void* handle) noexcept = 0;
};
```

### TlsConfig

```cpp
struct TlsConfig {
    bool verify_peer = true;          // 是否验证服务端证书
    bool enable_sni = true;           // 是否发送 SNI
    std::string ca_cert_path;         // 自定义 CA 路径（空=系统默认）
    std::string client_cert_path;     // 客户端证书路径（双向 TLS）
    std::string client_key_path;      // 客户端私钥路径
    int min_protocol_version = 0;     // 0=默认(TLS 1.2+), 12=TLS 1.2, 13=TLS 1.3
};
```

### 后端选择

| 后端 | CMake 值 | 平台 | 依赖 | 编译宏 |
|------|---------|------|------|--------|
| MbedTLS | `mbedtls` | macOS/Linux | vendor | `BEN_GEAR_TLS_MBEDTLS` |
| OpenSSL | `openssl` | 全平台 | 系统 OpenSSL | `BEN_GEAR_TLS_OPENSSL` |
| Schannel | `schannel` | Windows | 系统库 | `BEN_GEAR_TLS_SCHANNEL` |
| 无 TLS | `none` | — | 无 | `BEN_GEAR_TLS_NONE` |

```bash
# 默认（MbedTLS）
cmake -DTLS_BACKEND=mbedtls ...

# 系统 OpenSSL
cmake -DTLS_BACKEND=openssl ...

# Windows 原生
cmake -DTLS_BACKEND=schannel ...

# 禁用 TLS
cmake -DTLS_BACKEND=none ...
```

### 全局实例管理

```cpp
// 获取全局引擎（延迟初始化，首次调用时创建默认后端）
TlsEngine& engine = global_tls_engine();

// 运行时替换后端（必须在使用前调用）
set_global_tls_engine(std::make_unique<MyCustomTlsEngine>());

// 创建编译期默认后端
auto engine = create_default_tls_engine();
```

### 连接池集成

**重构前**（类型不安全）：
```cpp
struct PooledConnection {
    TcpStream stream;
    void* tls_state = nullptr;  // SSL*，手动 SSL_free()
};

Task<std::pair<TcpStream, void*>> acquire(...);
void release(..., void* tls_state = nullptr);
```

**重构后**（类型安全 + RAII）：
```cpp
struct PooledConnection {
    TcpStream stream;
    std::unique_ptr<TlsEngine::Session> tls_session;  // RAII 自动释放
};

Task<std::pair<TcpStream, std::unique_ptr<TlsEngine::Session>>> acquire(...);
void release(..., std::unique_ptr<TlsEngine::Session> tls_session = nullptr);
```

优势：
- **类型安全**：消除 `static_cast<SSL*>` 裸指针转换
- **RAII**：`unique_ptr` 析构自动释放 TLS 会话，无需手动 `SSL_free()`
- **后端无关**：连接池代码不包含任何 OpenSSL 头文件

### Transport 变更

**重构前**：
```cpp
class Transport {
    SSL_CTX* ctx_ = nullptr;
    SSL* ssl_ = nullptr;
    // 手动管理 ctx/ssl 生命周期
};
```

**重构后**：
```cpp
class Transport {
    std::unique_ptr<TlsEngine::Session> tls_session_;
    TlsConfig tls_config_;
    // RAII 自动管理，无需手动释放
};
```

### 文件结构

```
include/ben_gear/base/net/tls/
├── tls_engine.hpp          # TlsEngine 抽象接口
└── tls_config.hpp          # TlsConfig 配置

src/net/tls/
├── tls_engine.cpp          # 全局实例管理 + 后端工厂
├── mbed_tls_engine.hpp     # MbedTLS 后端声明
├── mbed_tls_engine.cpp     # MbedTLS 后端实现
├── openssl_engine.hpp      # OpenSSL 后端声明
├── openssl_engine.cpp      # OpenSSL 后端实现
├── schannel_engine.hpp     # Schannel 后端声明
└── schannel_engine.cpp     # Schannel 后端实现
```

---

## 压缩抽象层

### 架构

```
┌──────────────────────────────────────┐
│  zip_extract / 其他模块              │  ← 通过 global_compress_engine() 调用
├──────────────────────────────────────┤
│  CompressEngine (抽象接口)           │  ← inflate / deflate
├──────────────────────────────────────┤
│  ZlibEngine                          │  ← zlib 后端
└──────────────────────────────────────┘
```

### CompressEngine 接口

```cpp
class CompressEngine {
public:
    virtual ~CompressEngine() = default;

    virtual bool inflate(const uint8_t* src, uint32_t src_len,
                         std::vector<uint8_t>& dst,
                         uint32_t expected_size) = 0;

    virtual bool deflate(const uint8_t* src, uint32_t src_len,
                         std::vector<uint8_t>& dst) = 0;

    virtual const char* name() const noexcept = 0;
};
```

### 后端选择

| 后端 | CMake 值 | 依赖 | 编译宏 |
|------|---------|------|--------|
| zlib | `zlib` | vendor（`third_party/zlib/`） | `BEN_GEAR_COMPRESS_ZLIB` |
| none | `none` | 无 | — |

### 全局实例管理

```cpp
// 获取全局引擎
CompressEngine& engine = global_compress_engine();

// 运行时替换
set_global_compress_engine(std::make_unique<MyCompressEngine>());

// 创建默认后端
auto engine = create_default_compress_engine();
```

### 使用示例

**重构前**（直接依赖 zlib）：
```cpp
#include <zlib.h>

z_stream stream{};
stream.next_in = const_cast<uint8_t*>(src);
stream.avail_in = src_len;
// ... 手动管理 z_stream ...
inflateInit2(&stream, -MAX_WBITS);
inflate(&stream, Z_FINISH);
inflateEnd(&stream);
```

**重构后**（后端无关）：
```cpp
#include "ben_gear/base/compress/compress_engine.hpp"

return net::global_compress_engine().inflate(src, src_len, dst, expected_size);
```

### 文件结构

```
include/ben_gear/base/compress/
└── compress_engine.hpp      # CompressEngine 抽象接口

src/compress/
├── compress_engine.cpp      # 全局实例管理
├── zlib_engine.hpp          # zlib 后端声明
└── zlib_engine.cpp          # zlib 后端实现
```

---

## CMake 变更

### 新增 CMake 选项

```cmake
# TLS 后端选择
set(TLS_BACKEND "mbedtls" CACHE STRING "TLS backend: openssl|mbedtls|schannel|none")

# 压缩后端选择
set(COMPRESS_BACKEND "zlib" CACHE STRING "Compress backend: zlib|none")
```

### 新增 CMake 目标

| 目标 | 说明 | 依赖 |
|------|------|------|
| `bengear_tls` | TLS 抽象层库 | `bengear_base` + 后端库 |
| `bengear_compress` | 压缩抽象层库 | — |

### 链接关系变更

**重构前**：
```
bengear_net → OpenSSL::SSL, OpenSSL::Crypto, bengear_base, z
```

**重构后**：
```
bengear_net → bengear_base, bengear_tls, bengear_compress
bengear_tls → bengear_base, ${BENGEAR_TLS_LIBS}
bengear_compress → ${BENGEAR_COMPRESS_LIBS}
```

---

## 第三方依赖变更

### 移除

| 依赖 | 原位置 | 替代 |
|------|--------|------|
| nlohmann/json | `third_party/nlohmann/json.hpp` | 自研 `container::Json` |
| googletest | `third_party/googletest/` | 自研 `test_framework.hpp` |
| glog | `third_party/glog/` | 自研 `test_framework.hpp` |

### 新增

| 依赖 | 位置 | 用途 |
|------|------|------|
| MbedTLS | `third_party/mbedtls/` | TLS 默认后端（vendor） |
| zlib | `third_party/zlib/` | 压缩后端（vendor） |

---

## 其他变更

### ContextPruner 日志级别调整

裁剪详情日志从 `log::info_fmt` 降级为 `log::debug_fmt`，减少正常路径日志噪音：

```cpp
// 重构前
log::info_fmt("context_pruner: msg[{}] hard prune, depth={}, ...", ...);

// 重构后
log::debug_fmt("context_pruner: msg[{}] hard prune, depth={}, ...", ...);
```

### JSON 注释清理

移除 `json.hpp` 中 "nlohmann 兼容" 相关注释，标记为自研实现：

```cpp
// 重构前
// API 与 nlohmann/json 兼容，业务代码零修改
// 初始化列表构造（nlohmann 兼容）
// items() 迭代（nlohmann 兼容）

// 重构后
// 高性能 JSON 值（自研，零外部依赖）
// 初始化列表构造
// items() 迭代
```
