#include "mbed_tls_engine.hpp"

#include "ben_gear/base/log/logger.hpp"
#include "ben_gear/base/platform/os.hpp"

#include <mbedtls/ssl.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/error.h>

#include <cstring>
#include <stdexcept>
#include <string>

namespace ben_gear::net {

// ==================== 内部结构 ====================

struct MbedContext {
    mbedtls_ssl_context ssl;
};

struct MbedIoContext {
    EventLoop* loop = nullptr;
    socket_handle fd = invalid_socket_handle;
};

/// MbedTlsEngine 全局状态（RNG + CA 证书）
struct MbedTlsState {
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_x509_crt cacert;
    bool rng_ready = false;
    bool cacert_loaded = false;
};

namespace {

// ==================== Timer 回调（MbedTLS 非阻塞 IO 必需）====================

void mbedtls_timer_set(void* ctx, uint32_t int_ms, uint32_t fin_ms) {
    auto* timer = static_cast<MbedTimer*>(ctx);
    if (fin_ms == 0) {
        timer->active = false;
        return;
    }
    timer->finish = std::chrono::steady_clock::now() + std::chrono::milliseconds(fin_ms);
    timer->active = true;
}

int mbedtls_timer_get(void* ctx) {
    auto* timer = static_cast<MbedTimer*>(ctx);
    if (!timer->active) return -1;
    if (std::chrono::steady_clock::now() >= timer->finish) return 2;  // 已超时
    return 0;  // 未超时
}

// ==================== 非阻塞 IO 回调 ====================

int mbedtls_send(void* ctx, const unsigned char* buf, size_t len) {
    auto* io = static_cast<MbedIoContext*>(ctx);
    auto fd = io->fd;
#ifdef _WIN32
    auto result = ::send(fd, reinterpret_cast<const char*>(buf), static_cast<int>(len), 0);
#else
    auto result = ::send(fd, buf, len, MSG_NOSIGNAL);
#endif
    if (result < 0) {
#ifdef _WIN32
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) return MBEDTLS_ERR_SSL_WANT_WRITE;
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK) return MBEDTLS_ERR_SSL_WANT_WRITE;
#endif
        return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    }
    return static_cast<int>(result);
}

int mbedtls_recv(void* ctx, unsigned char* buf, size_t len) {
    auto* io = static_cast<MbedIoContext*>(ctx);
    auto fd = io->fd;
#ifdef _WIN32
    auto result = ::recv(fd, reinterpret_cast<char*>(buf), static_cast<int>(len), 0);
#else
    auto result = ::recv(fd, buf, len, 0);
#endif
    if (result < 0) {
#ifdef _WIN32
        int err = WSAGetLastError();
        if (err == WSAEWOULDBLOCK) return MBEDTLS_ERR_SSL_WANT_READ;
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK) return MBEDTLS_ERR_SSL_WANT_READ;
#endif
        return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
    }
    if (result == 0) {
        return MBEDTLS_ERR_SSL_CONN_EOF;
    }
    return static_cast<int>(result);
}

// CTR_DRBG 随机数回调（供 mbedtls_ssl_conf_rng 使用）
int rng_callback(void* ctx, unsigned char* output, size_t len) {
    auto* ctr_drbg = static_cast<mbedtls_ctr_drbg_context*>(ctx);
    return mbedtls_ctr_drbg_random(ctr_drbg, output, len);
}

/// 加载系统默认 CA 证书
bool load_system_cacert(mbedtls_x509_crt* cacert) {
    const char* paths[] = {
#if BEN_GEAR_PLATFORM_MACOS
        "/etc/ssl/cert.pem",
        "/usr/local/etc/openssl/cert.pem",
        "/opt/homebrew/etc/openssl/cert.pem",
#elif BEN_GEAR_PLATFORM_LINUX
        "/etc/ssl/certs/ca-certificates.crt",
        "/etc/pki/tls/certs/ca-bundle.crt",
        "/etc/ssl/ca-bundle.pem",
#endif
        nullptr
    };

    for (int i = 0; paths[i]; ++i) {
        int ret = mbedtls_x509_crt_parse_file(cacert, paths[i]);
        if (ret == 0) {
            log::info_fmt("MbedTlsEngine: loaded CA certs from {}", paths[i]);
            return true;
        }
    }

    log::warn_fmt("MbedTlsEngine: no system CA certs found, TLS verification may fail");
    return false;
}

}  // namespace

// ==================== MbedTlsEngine::Session ====================

MbedTlsEngine::Session::Session() {
    io_ctx_ = new MbedIoContext();
    ctx_ = new MbedContext();
    timer_ = new MbedTimer();
    mbedtls_ssl_init(&ctx_->ssl);
}

MbedTlsEngine::Session::~Session() {
    delete io_ctx_;
    io_ctx_ = nullptr;
    delete timer_;
    timer_ = nullptr;
    if (ctx_) {
        mbedtls_ssl_free(&ctx_->ssl);
        delete ctx_;
        ctx_ = nullptr;
    }
    if (ssl_conf_) {
        auto* conf = static_cast<mbedtls_ssl_config*>(ssl_conf_);
        mbedtls_ssl_config_free(conf);
        delete conf;
        ssl_conf_ = nullptr;
    }
}

Task<void> MbedTlsEngine::Session::handshake(EventLoop& loop, socket_handle fd,
                                              std::string_view host,
                                              const TlsConfig& config) {
    io_ctx_->loop = &loop;
    io_ctx_->fd = fd;

    // 获取引擎全局状态
    auto& engine = static_cast<MbedTlsEngine&>(global_tls_engine());
    auto* state = static_cast<MbedTlsState*>(engine.state_);

    if (!state || !state->rng_ready) {
        throw std::runtime_error("MbedTlsEngine: not initialized (RNG not ready)");
    }

    // 配置 SSL（每个 session 独立配置，堆分配确保生命周期覆盖 SSL context）
    auto* conf = new mbedtls_ssl_config();
    mbedtls_ssl_config_init(conf);

    int ret = mbedtls_ssl_config_defaults(conf, MBEDTLS_SSL_IS_CLIENT,
                                           MBEDTLS_SSL_TRANSPORT_STREAM,
                                           MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        char err_buf[128];
        mbedtls_strerror(ret, err_buf, sizeof(err_buf));
        mbedtls_ssl_config_free(conf);
        delete conf;
        throw std::runtime_error(std::string("MbedTlsEngine: ssl_config_defaults failed: ") + err_buf);
    }

    // 配置 RNG（MbedTLS 3.x 必需）
    mbedtls_ssl_conf_rng(conf, rng_callback, &state->ctr_drbg);

    // CA 证书
    if (config.verify_peer) {
        if (state->cacert_loaded) {
            mbedtls_ssl_conf_ca_chain(conf, &state->cacert, nullptr);
        }
        mbedtls_ssl_conf_authmode(conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    } else {
        mbedtls_ssl_conf_authmode(conf, MBEDTLS_SSL_VERIFY_NONE);
    }

    // 协议版本
    if (config.min_protocol_version >= 13) {
        mbedtls_ssl_conf_min_tls_version(conf, MBEDTLS_SSL_VERSION_TLS1_3);
    } else if (config.min_protocol_version >= 12) {
        mbedtls_ssl_conf_min_tls_version(conf, MBEDTLS_SSL_VERSION_TLS1_2);
    }

    // ssl_setup 内部直接引用 conf 指针，不做深复制
    // conf 必须在 SSL context 生命周期内保持有效
    ret = mbedtls_ssl_setup(&ctx_->ssl, conf);
    if (ret != 0) {
        char err_buf[128];
        mbedtls_strerror(ret, err_buf, sizeof(err_buf));
        mbedtls_ssl_config_free(conf);
        delete conf;
        throw std::runtime_error(std::string("MbedTlsEngine: ssl_setup failed: ") + err_buf);
    }
    // 保存配置（随 Session 生命周期释放，必须先释放 ssl 再释放 conf）
    ssl_conf_ = conf;

    // SNI
    if (config.enable_sni && !host.empty()) {
        std::string host_str(host);
        mbedtls_ssl_set_hostname(&ctx_->ssl, host_str.c_str());
    }

    // 设置非阻塞 IO 回调
    mbedtls_ssl_set_bio(&ctx_->ssl, io_ctx_, mbedtls_send, mbedtls_recv, nullptr);

    // 设置定时器回调（MbedTLS 非阻塞模式必需）
    mbedtls_ssl_set_timer_cb(&ctx_->ssl, timer_, mbedtls_timer_set, mbedtls_timer_get);

    // 握手循环
    for (;;) {
        ret = mbedtls_ssl_handshake(&ctx_->ssl);
        if (ret == 0) {
            connected_ = true;
            log::debug_fmt("MbedTlsEngine: handshake completed for {}", host);
            co_return;
        }
        if (ret == MBEDTLS_ERR_SSL_WANT_READ) {
            co_await loop.wait_read(fd);
            continue;
        }
        if (ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            co_await loop.wait_write(fd);
            continue;
        }

        char err_buf[128];
        mbedtls_strerror(ret, err_buf, sizeof(err_buf));
        log::error_fmt("MbedTlsEngine: handshake failed: {}", err_buf);
        throw std::runtime_error(std::string("TLS handshake failed: ") + err_buf);
    }
}

Task<void> MbedTlsEngine::Session::write_all(EventLoop& loop, std::string_view data) {
    std::size_t written = 0;
    while (written < data.size()) {
        int ret = mbedtls_ssl_write(&ctx_->ssl,
                                    reinterpret_cast<const unsigned char*>(data.data() + written),
                                    data.size() - written);
        if (ret > 0) {
            written += static_cast<std::size_t>(ret);
            continue;
        }
        if (ret == MBEDTLS_ERR_SSL_WANT_READ) {
            co_await loop.wait_read(io_ctx_->fd);
            continue;
        }
        if (ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            co_await loop.wait_write(io_ctx_->fd);
            continue;
        }
        {
            char err_buf[128];
            mbedtls_strerror(ret, err_buf, sizeof(err_buf));
            log::error_fmt("MbedTlsEngine: write failed: ret={}, {}", ret, err_buf);
            throw std::runtime_error(std::string("TLS write failed: ") + err_buf);
        }
    }
}

Task<std::size_t> MbedTlsEngine::Session::read_some(EventLoop& loop,
                                                     char* buf, std::size_t size) {
    for (;;) {
        int ret = mbedtls_ssl_read(&ctx_->ssl,
                                   reinterpret_cast<unsigned char*>(buf),
                                   size);
        if (ret > 0) {
            co_return static_cast<std::size_t>(ret);
        }
        if (ret == 0) {
            co_return 0;
        }
        if (ret == MBEDTLS_ERR_SSL_WANT_READ) {
            co_await loop.wait_read(io_ctx_->fd);
            continue;
        }
        if (ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            co_await loop.wait_write(io_ctx_->fd);
            continue;
        }
        // 对端关闭连接
        if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            co_return 0;
        }
        {
            char err_buf[128];
            mbedtls_strerror(ret, err_buf, sizeof(err_buf));
            log::error_fmt("MbedTlsEngine: read failed: ret={}, {}", ret, err_buf);
            throw std::runtime_error(std::string("TLS read failed: ") + err_buf);
        }
    }
}

void* MbedTlsEngine::Session::native_handle() noexcept {
    // 返回 Session 自身（包含 ctx + conf + timer + io）
    // 连接池通过 free_native_handle 销毁整个 Session
    return this;
}

void MbedTlsEngine::Session::shutdown() noexcept {
    if (ctx_ && connected_) {
        mbedtls_ssl_close_notify(&ctx_->ssl);
        connected_ = false;
    }
}

bool MbedTlsEngine::Session::is_connected() const noexcept {
    return connected_;
}

// ==================== MbedTlsEngine ====================

MbedTlsEngine::MbedTlsEngine() = default;

MbedTlsEngine::~MbedTlsEngine() {
    if (state_) {
        auto* s = static_cast<MbedTlsState*>(state_);
        if (s->cacert_loaded) {
            mbedtls_x509_crt_free(&s->cacert);
        }
        if (s->rng_ready) {
            mbedtls_ctr_drbg_free(&s->ctr_drbg);
            mbedtls_entropy_free(&s->entropy);
        }
        delete s;
        state_ = nullptr;
    }
}

std::unique_ptr<TlsEngine::Session> MbedTlsEngine::create_session() {
    return std::make_unique<Session>();
}

void MbedTlsEngine::initialize() {
    if (initialized_) return;

    auto* s = new MbedTlsState();

    // 1. 初始化 Entropy + CTR_DRBG（RNG）
    mbedtls_entropy_init(&s->entropy);
    mbedtls_ctr_drbg_init(&s->ctr_drbg);

    // 使用默认 entropy 源 seeding
    int ret = mbedtls_ctr_drbg_seed(&s->ctr_drbg, mbedtls_entropy_func,
                                     &s->entropy, nullptr, 0);
    if (ret != 0) {
        char err_buf[128];
        mbedtls_strerror(ret, err_buf, sizeof(err_buf));
        log::error_fmt("MbedTlsEngine: ctr_drbg_seed failed: {}", err_buf);
        mbedtls_ctr_drbg_free(&s->ctr_drbg);
        mbedtls_entropy_free(&s->entropy);
        delete s;
        throw std::runtime_error("MbedTlsEngine: RNG initialization failed");
    }
    s->rng_ready = true;
    log::info_fmt("MbedTlsEngine: RNG initialized (CTR_DRBG)");

    // 2. 加载系统 CA 证书
    mbedtls_x509_crt_init(&s->cacert);
    s->cacert_loaded = load_system_cacert(&s->cacert);

    state_ = s;
    initialized_ = true;
    log::info_fmt("MbedTlsEngine: initialized");
}

void MbedTlsEngine::free_native_handle(void* handle) noexcept {
    if (handle) {
        auto* session = static_cast<Session*>(handle);
        // 先 shutdown 再析构（析构中会 free ssl + conf + timer + io）
        if (session->is_connected()) {
            session->shutdown();
        }
        delete session;
    }
}

}  // namespace ben_gear::net
