#include "ben_gear/base/net/tls/tls_engine.hpp"

#include "ben_gear/base/log/logger.hpp"
#include "ben_gear/base/platform/os.hpp"

#include <memory>
#include <mutex>

// 条件引入后端头文件
#if defined(BEN_GEAR_TLS_MBEDTLS)
#include "mbed_tls_engine.hpp"
#elif defined(BEN_GEAR_TLS_OPENSSL)
#include "openssl_engine.hpp"
#elif defined(BEN_GEAR_TLS_SCHANNEL)
#include "schannel_engine.hpp"
#endif

namespace ben_gear::net {

namespace {

std::mutex g_tls_mutex;
std::unique_ptr<TlsEngine> g_tls_engine;

}  // namespace

TlsEngine& global_tls_engine() {
    std::lock_guard lock(g_tls_mutex);
    if (!g_tls_engine) {
        g_tls_engine = create_default_tls_engine();
        if (g_tls_engine) {
            g_tls_engine->initialize();
            log::info_fmt("TlsEngine: using backend {}", g_tls_engine->name());
        }
    }
    return *g_tls_engine;
}

void set_global_tls_engine(std::unique_ptr<TlsEngine> engine) {
    std::lock_guard lock(g_tls_mutex);
    if (engine) {
        engine->initialize();
        log::info_fmt("TlsEngine: set backend {}", engine->name());
    }
    g_tls_engine = std::move(engine);
}

std::unique_ptr<TlsEngine> create_default_tls_engine() {
#if defined(BEN_GEAR_TLS_MBEDTLS)
    log::info_fmt("TlsEngine: creating MbedTLS backend");
    return std::make_unique<MbedTlsEngine>();
#elif defined(BEN_GEAR_TLS_OPENSSL)
    log::info_fmt("TlsEngine: creating OpenSSL backend");
    return std::make_unique<OpenSslEngine>();
#elif defined(BEN_GEAR_TLS_SCHANNEL)
    log::info_fmt("TlsEngine: creating Schannel backend");
    return std::make_unique<SchannelEngine>();
#else
    // 按平台自动选择
    // macOS/Linux → MbedTLS, Windows → Schannel
#if BEN_GEAR_PLATFORM_WINDOWS
    log::info_fmt("TlsEngine: creating Schannel backend (auto-detected Windows)");
    return std::make_unique<SchannelEngine>();
#else
    log::info_fmt("TlsEngine: creating MbedTLS backend (auto-detected Unix)");
    return std::make_unique<MbedTlsEngine>();
#endif
#endif
}

}  // namespace ben_gear::net
