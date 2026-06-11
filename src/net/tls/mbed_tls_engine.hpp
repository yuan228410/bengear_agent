#pragma once

#include "ben_gear/base/net/tls/tls_engine.hpp"

#include "ben_gear/base/net/event_loop.hpp"
#include "ben_gear/base/net/socket.hpp"
#include "ben_gear/base/net/task.hpp"

#include <cstddef>
#include <chrono>
#include <memory>
#include <string_view>
#include <vector>

namespace ben_gear::net {

/// MbedTLS 定时器（非阻塞 IO 必需）
struct MbedTimer {
    std::chrono::steady_clock::time_point finish;
    bool active = false;
};

/// MbedTLS IO 上下文（定义在 .cpp 中）
struct MbedIoContext;

/// MbedTLS 内部上下文（定义在 .cpp 中）
struct MbedContext;

/// MbedTLS TLS 后端
class MbedTlsEngine : public TlsEngine {
public:
    class Session : public TlsEngine::Session {
    public:
        Session();
        ~Session() override;

        Session(const Session&) = delete;
        Session& operator=(const Session&) = delete;

        Task<void> handshake(EventLoop& loop, socket_handle fd,
                             std::string_view host,
                             const TlsConfig& config) override;
        Task<void> write_all(EventLoop& loop, std::string_view data) override;
        Task<std::size_t> read_some(EventLoop& loop, char* buf, std::size_t size) override;
        void* native_handle() noexcept override;
        void shutdown() noexcept override;
        bool is_connected() const noexcept override;

    private:
        MbedIoContext* io_ctx_ = nullptr;
        MbedContext* ctx_ = nullptr;         // SSL context
        MbedTimer* timer_ = nullptr;         // 握手定时器
        void* ssl_conf_ = nullptr;           // mbedtls_ssl_config*（必须比 ctx_ 长命）
        bool connected_ = false;
    };

    MbedTlsEngine();
    ~MbedTlsEngine() override;

    std::unique_ptr<TlsEngine::Session> create_session() override;
    void initialize() override;
    const char* name() const noexcept override { return "mbedtls"; }
    void free_native_handle(void* handle) noexcept override;

private:
    void* state_ = nullptr;  // MbedTlsState*（定义在 .cpp 中）
    bool initialized_ = false;
};

}  // namespace ben_gear::net
