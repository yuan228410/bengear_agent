#include "compress/compress_engine.hpp"

#include "compress/zlib_engine.hpp"

#include "log/logger.hpp"

#include <memory>

namespace ben_gear::compress {

std::unique_ptr<CompressEngine> create_default_compress_engine() {
    auto engine = std::make_unique<ZlibEngine>();
    log::info_fmt("CompressEngine: using backend {}", engine->name());
    return engine;
}

}  // namespace ben_gear::compress
