#include "openssl_engine.hpp"

#include "ben_gear/base/log/logger.hpp"

#include <openssl/ssl.h>

#include <stdexcept>
#include <string>

namespace ben_gear::net {

// ==================== OpenSslEngine::Session ====================

OpenSslEngine::Session::~Session() {
    if (ssl_) {
        SSL_free(static_cast<SSL*>(ssl_));
        ssl_ = nullptr;
    }
    if (ssl_ctx_) {
        SSL_CTX_free(static_cast<SSL_CTX*>(ssl_ctx_));
        ssl_ctx_ = nullptr;
    }
}

Task<void> OpenSslEngine::Session::handshake(EventLoop& loop, socket_handle fd,
                                              std::string_view host,
                                              const TlsConfig& config) {
    auto* ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) {
        throw std::runtime_error("SSL_CTX_new failed");
    }
    ssl_ctx_ = ctx;

    if (config.verify_peer) {
        if (config.ca_cert_path.empty()) {
            SSL_CTX_set_default_verify_paths(ctx);
        } else {
            SSL_CTX_load_verify_locations(ctx, config.ca_cert_path.c_str(), nullptr);
        }
    }

    auto* ssl = SSL_new(ctx);
    if (!ssl) {
        throw std::runtime_error("SSL_new failed");
    }
    ssl_ = ssl;

    SSL_set_fd(ssl, static_cast<int>(static_cast<intptr_t>(fd)));

    // SNI
    if (config.enable_sni && !host.empty()) {
        std::string host_str(host);
        SSL_set_tlsext_host_name(ssl, host_str.c_str());
        SSL_set1_host(ssl, host_str.c_str());
    }

    // 证书验证
    if (config.verify_peer) {
        SSL_set_verify(ssl, SSL_VERIFY_PEER, nullptr);
    } else {
        SSL_set_verify(ssl, SSL_VERIFY_NONE, nullptr);
    }

    // 协议版本
    if (config.min_protocol_version >= 12) {
        SSL_set_min_proto_version(ssl, TLS1_2_VERSION);
    }
    if (config.min_protocol_version >= 13) {
        SSL_set_min_proto_version(ssl, TLS1_3_VERSION);
    }

    for (;;) {
        const int result = SSL_connect(ssl);
        if (result == 1) {
            connected_ = true;
            co_return;
        }
        co_await wait_ssl(loop, fd, "TLS handshake failed", result);
    }
}

Task<void> OpenSslEngine::Session::write_all(EventLoop& loop, std::string_view data) {
    auto* ssl = static_cast<SSL*>(ssl_);
    std::size_t written = 0;
    while (written < data.size()) {
        const int result = SSL_write(ssl, data.data() + written,
                                     static_cast<int>(data.size() - written));
        if (result > 0) {
            written += static_cast<std::size_t>(result);
            continue;
        }
        // 需要 fd 用于 wait，从 SSL_get_fd 获取
        auto fd_val = static_cast<socket_handle>(SSL_get_fd(ssl));
        co_await wait_ssl(loop, fd_val, "tls write failed", result);
    }
}

Task<std::size_t> OpenSslEngine::Session::read_some(EventLoop& loop,
                                                     char* buf, std::size_t size) {
    auto* ssl = static_cast<SSL*>(ssl_);
    for (;;) {
        const int result = SSL_read(ssl, buf, static_cast<int>(size));
        if (result > 0) {
            co_return static_cast<std::size_t>(result);
        }
        const int error = SSL_get_error(ssl, result);
        if (error == SSL_ERROR_ZERO_RETURN) {
            co_return 0;
        }
        auto fd_val = static_cast<socket_handle>(SSL_get_fd(ssl));
        co_await wait_ssl(loop, fd_val, "tls read failed", result);
    }
}

void* OpenSslEngine::Session::native_handle() noexcept {
    return ssl_;
}

void OpenSslEngine::Session::shutdown() noexcept {
    if (ssl_ && connected_) {
        SSL_shutdown(static_cast<SSL*>(ssl_));
        connected_ = false;
    }
}

bool OpenSslEngine::Session::is_connected() const noexcept {
    return connected_;
}

Task<void> OpenSslEngine::Session::wait_ssl(EventLoop& loop, socket_handle fd,
                                             const char* message, int result) {
    auto* ssl = static_cast<SSL*>(ssl_);
    const int error = SSL_get_error(ssl, result);
    if (error == SSL_ERROR_WANT_READ) {
        co_await loop.wait_read(fd);
        co_return;
    }
    if (error == SSL_ERROR_WANT_WRITE) {
        co_await loop.wait_write(fd);
        co_return;
    }
    throw std::runtime_error(message);
}

// ==================== OpenSslEngine ====================

std::unique_ptr<TlsEngine::Session> OpenSslEngine::create_session() {
    return std::make_unique<OpenSslEngine::Session>();
}

void OpenSslEngine::initialize() {
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_ssl_algorithms();
    log::info_fmt("OpenSslEngine: initialized");
}

void OpenSslEngine::free_native_handle(void* handle) noexcept {
    if (handle) {
        SSL_free(static_cast<SSL*>(handle));
    }
}

}  // namespace ben_gear::net
