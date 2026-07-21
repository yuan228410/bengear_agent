#include "net/tls/tls_engine.hpp"

#include "log/logger.hpp"
#include "platform/os.hpp"

#include <memory>

// 条件引入后端头文件
#if defined(BEN_GEAR_TLS_MBEDTLS)
#include "mbed_tls_engine.hpp"
#elif defined(BEN_GEAR_TLS_OPENSSL)
#include "openssl_engine.hpp"
#elif defined(BEN_GEAR_TLS_SCHANNEL)
#include "schannel_engine.hpp"
#endif

namespace ben_gear::net {

std::unique_ptr<TlsEngine> create_default_tls_engine() {
#if defined(BEN_GEAR_TLS_MBEDTLS)
    log::info_fmt("TlsEngine: creating MbedTLS backend");
    auto engine = std::make_unique<MbedTlsEngine>();
#elif defined(BEN_GEAR_TLS_OPENSSL)
    log::info_fmt("TlsEngine: creating OpenSSL backend");
    auto engine = std::make_unique<OpenSslEngine>();
#elif defined(BEN_GEAR_TLS_SCHANNEL)
    log::info_fmt("TlsEngine: creating Schannel backend");
    auto engine = std::make_unique<SchannelEngine>();
#else
#  if BEN_GEAR_PLATFORM_WINDOWS
    log::info_fmt("TlsEngine: creating Schannel backend (auto-detected Windows)");
    auto engine = std::make_unique<SchannelEngine>();
#  else
    log::info_fmt("TlsEngine: creating MbedTLS backend (auto-detected Unix)");
    auto engine = std::make_unique<MbedTlsEngine>();
#  endif
#endif
    if (engine) {
        engine->initialize();
        log::info_fmt("TlsEngine: using backend {}", engine->name());
    }
    return engine;
}

}  // namespace ben_gear::net
