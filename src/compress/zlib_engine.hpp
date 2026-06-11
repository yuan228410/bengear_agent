#pragma once

#include "ben_gear/base/compress/compress_engine.hpp"

namespace ben_gear::net {

/// zlib 压缩后端（vendor 或系统）
class ZlibEngine : public CompressEngine {
public:
    bool inflate(const uint8_t* src, uint32_t src_len,
                 std::vector<uint8_t>& dst,
                 uint32_t expected_size) override;
    bool deflate(const uint8_t* src, uint32_t src_len,
                 std::vector<uint8_t>& dst) override;
    const char* name() const noexcept override { return "zlib"; }
};

}  // namespace ben_gear::net
