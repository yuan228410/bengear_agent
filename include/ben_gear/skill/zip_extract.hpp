#pragma once

#include "ben_gear/base/net/http.hpp"
#include "ben_gear/base/net/io_context.hpp"
#include "ben_gear/base/platform/os.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace ben_gear::skill {

/// 使用 HttpClient 下载文件，失败回退到 curl / wget
bool download_file(const std::string& url, const std::filesystem::path& dest,
                   net::IoContext& io_ctx, bool expect_zip = false);

// ── ZIP 格式常量 ──────────────────────────────────────────
constexpr uint32_t kLocalFileHeaderSig = 0x04034b50;
constexpr uint32_t kCentralDirSig = 0x02014b50;
constexpr uint32_t kEndCentralDirSig = 0x06054b50;
constexpr uint16_t kMethodStored = 0;
constexpr uint16_t kMethodDeflated = 8;

/// 读取小端 16-bit（内联，热点路径）
inline uint16_t read_u16_le(const uint8_t* p) { return p[0] | (p[1] << 8); }
/// 读取小端 32-bit（内联，热点路径）
inline uint32_t read_u32_le(const uint8_t* p) {
    return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24);
}

/// 使用 zlib 解压 deflate 数据
bool inflate_data(const uint8_t* src, uint32_t src_len,
                  std::vector<uint8_t>& dst, uint32_t expected_size);

/// 使用 zlib 直接解压 ZIP 文件（无需第三方 zip 库）
bool extract_zip(const std::filesystem::path& zip_path,
                 const std::filesystem::path& target_dir);

}  // namespace ben_gear::skill
