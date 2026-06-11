#pragma once

#include "ben_gear/base/net/tls/tls_engine.hpp"

#include "ben_gear/base/net/event_loop.hpp"
#include "ben_gear/base/net/socket.hpp"
#include "ben_gear/base/net/task.hpp"

#include <cstddef>
#include <memory>
#include <string_view>

namespace ben_gear::net {

/// OpenSSL TLS 后端
class OpenSslEngine : public TlsEngine {
public:
    class Session : public TlsEngine::Session {
    public:
        Session() = default;
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
        Task<void> wait_ssl(EventLoop& loop, socket_handle fd, const char* message, int result);

        void* ssl_ctx_ = nullptr;  // SSL_CTX*
        void* ssl_ = nullptr;      // SSL*
        bool connected_ = false;
    };

    std::unique_ptr<TlsEngine::Session> create_session() override;
    void initialize() override;
    const char* name() const noexcept override { return "openssl"; }
    void free_native_handle(void* handle) noexcept override;
};

}  // namespace ben_gear::net
