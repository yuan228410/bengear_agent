#pragma once

#include "ben_gear/base/net/tls/tls_engine.hpp"

#include "ben_gear/base/net/event_loop.hpp"
#include "ben_gear/base/net/socket.hpp"
#include "ben_gear/base/net/task.hpp"

#include <cstddef>
#include <memory>
#include <string_view>
#include <vector>

namespace ben_gear::net {

/// Schannel TLS 后端（Windows 原生）
///
/// 使用 Windows SSPI/Schannel API 实现 TLS，零外部依赖。
/// 自动使用 Windows 证书存储，自动支持 TLS 1.3（Win10+）。
class SchannelEngine : public TlsEngine {
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
        struct Impl;
        Impl* impl_ = nullptr;
    };

    SchannelEngine();
    ~SchannelEngine() override;

    std::unique_ptr<TlsEngine::Session> create_session() override;
    void initialize() override;
    const char* name() const noexcept override { return "schannel"; }
    void free_native_handle(void* handle) noexcept override;

private:
    bool initialized_ = false;
};

}  // namespace ben_gear::net
