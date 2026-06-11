#include "ben_gear/base/compress/compress_engine.hpp"

#include "zlib_engine.hpp"

#include "ben_gear/base/log/logger.hpp"

#include <memory>
#include <mutex>

namespace ben_gear::net {

namespace {

std::mutex g_compress_mutex;
std::unique_ptr<CompressEngine> g_compress_engine;

}  // namespace

CompressEngine& global_compress_engine() {
    std::lock_guard lock(g_compress_mutex);
    if (!g_compress_engine) {
        g_compress_engine = create_default_compress_engine();
        if (g_compress_engine) {
            log::info_fmt("CompressEngine: using backend {}", g_compress_engine->name());
        }
    }
    return *g_compress_engine;
}

void set_global_compress_engine(std::unique_ptr<CompressEngine> engine) {
    std::lock_guard lock(g_compress_mutex);
    if (engine) {
        log::info_fmt("CompressEngine: set backend {}", engine->name());
    }
    g_compress_engine = std::move(engine);
}

std::unique_ptr<CompressEngine> create_default_compress_engine() {
    return std::make_unique<ZlibEngine>();
}

}  // namespace ben_gear::net
