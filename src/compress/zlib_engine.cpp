#include "zlib_engine.hpp"

#include "ben_gear/base/log/logger.hpp"

#include <zlib.h>

namespace ben_gear::net {

bool ZlibEngine::inflate(const uint8_t* src, uint32_t src_len,
                          std::vector<uint8_t>& dst,
                          uint32_t expected_size) {
    if (expected_size == 0) {
        expected_size = src_len * 4;  // 默认 4 倍膨胀
    }
    dst.resize(expected_size);

    z_stream stream = {};
    stream.next_in = const_cast<Bytef*>(src);
    stream.avail_in = src_len;
    stream.next_out = dst.data();
    stream.avail_out = dst.size();

    // raw deflate（-MAX_WBITS 表示无 zlib/gzip header）
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        log::error_fmt("ZlibEngine: inflateInit2 failed");
        return false;
    }

    int ret = ::inflate(&stream, Z_FINISH);
    inflateEnd(&stream);

    if (ret != Z_STREAM_END) {
        log::error_fmt("ZlibEngine: inflate failed: ret={}", ret);
        return false;
    }

    dst.resize(stream.total_out);
    return true;
}

bool ZlibEngine::deflate(const uint8_t* src, uint32_t src_len,
                          std::vector<uint8_t>& dst) {
    // 预估压缩后大小
    auto bound = compressBound(src_len);
    dst.resize(bound);

    z_stream stream = {};
    stream.next_in = const_cast<Bytef*>(src);
    stream.avail_in = src_len;
    stream.next_out = dst.data();
    stream.avail_out = dst.size();

    if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                     -MAX_WBITS, MAX_MEM_LEVEL, Z_DEFAULT_STRATEGY) != Z_OK) {
        log::error_fmt("ZlibEngine: deflateInit2 failed");
        return false;
    }

    int ret = ::deflate(&stream, Z_FINISH);
    deflateEnd(&stream);

    if (ret != Z_STREAM_END) {
        log::error_fmt("ZlibEngine: deflate failed: ret={}", ret);
        return false;
    }

    dst.resize(stream.total_out);
    return true;
}

}  // namespace ben_gear::net
