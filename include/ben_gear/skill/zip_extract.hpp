#pragma once

#include "ben_gear/base/net/http.hpp"
#include "ben_gear/base/log/logger.hpp"

#include "ben_gear/base/platform/os.hpp"

#include <zlib.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace ben_gear::skill {

/// 使用 HttpClient 下载文件，失败回退到 curl / wget
inline bool download_file(const std::string& url, const std::filesystem::path& dest) {
    log::info_fmt("downloading: {} -> {}", url, dest.string());

    // 1. 优先使用自有 HttpClient
    try {
        net::HttpClient client;
        auto resp = client.get(url, {});
        if (resp.status == 200 && !resp.body.empty()) {
            std::ofstream of(dest, std::ios::binary);
            if (of) {
                of.write(resp.body.data(), static_cast<std::streamsize>(resp.body.size()));
                log::info_fmt("downloaded via HttpClient: {} ({} bytes)", url, resp.body.size());
                return true;
            }
        }
        log::debug_fmt("HttpClient download got status: {}", resp.status);
    } catch (const std::exception& e) {
        log::debug_fmt("HttpClient download failed: {}", e.what());
    }

    // 2. 回退到 curl
    std::string curl_cmd = "curl -sS -L -o \"" + dest.string() + "\" \"" + url + "\""
#if BEN_GEAR_PLATFORM_POSIX
        " 2>/dev/null"
#endif
        ;
    if (std::system(curl_cmd.c_str()) == 0 && std::filesystem::exists(dest) && std::filesystem::file_size(dest) > 0) {
        log::info_fmt("downloaded via curl: {}", url);
        return true;
    }
    log::debug_fmt("curl download failed for: {}", url);

    // 3. 回退到 wget
    std::string wget_cmd = "wget -q -O \"" + dest.string() + "\" \"" + url + "\""
#if BEN_GEAR_PLATFORM_POSIX
        " 2>/dev/null"
#endif
        ;
    if (std::system(wget_cmd.c_str()) == 0 && std::filesystem::exists(dest) && std::filesystem::file_size(dest) > 0) {
        log::info_fmt("downloaded via wget: {}", url);
        return true;
    }
    log::debug_fmt("wget download failed for: {}", url);

    log::error_fmt("all download methods failed for: {}", url);
    return false;
}

// ── ZIP 格式常量 ──────────────────────────────────────────
constexpr uint32_t kLocalFileHeaderSig = 0x04034b50;
constexpr uint32_t kCentralDirSig = 0x02014b50;
constexpr uint32_t kEndCentralDirSig = 0x06054b50;
constexpr uint16_t kMethodStored = 0;
constexpr uint16_t kMethodDeflated = 8;

/// 读取小端 16-bit
inline uint16_t read_u16_le(const uint8_t* p) { return p[0] | (p[1] << 8); }
/// 读取小端 32-bit
inline uint32_t read_u32_le(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24); }

/// 使用 zlib 解压 deflate 数据
inline bool inflate_data(const uint8_t* src, uint32_t src_len,
                         std::vector<uint8_t>& dst, uint32_t expected_size) {
    dst.resize(expected_size);
    z_stream stream{};
    stream.next_in = const_cast<uint8_t*>(src);
    stream.avail_in = src_len;
    stream.next_out = dst.data();
    stream.avail_out = expected_size;

    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        log::error_fmt("inflateInit2 failed");
        return false;
    }
    int ret = inflate(&stream, Z_FINISH);
    inflateEnd(&stream);

    if (ret != Z_STREAM_END) {
        log::error_fmt("inflate failed: ret={}", ret);
        return false;
    }
    dst.resize(stream.total_out);
    return true;
}

/// 使用 zlib 直接解压 ZIP 文件（无需第三方 zip 库）
inline bool extract_zip(const std::filesystem::path& zip_path,
                        const std::filesystem::path& target_dir) {
    log::info_fmt("extracting zip: {} -> {}", zip_path.string(), target_dir.string());

    std::error_code ec;
    std::filesystem::create_directories(target_dir, ec);
    if (ec) {
        log::error_fmt("failed to create target dir: {}", ec.message());
        return false;
    }

    // 读取整个 zip 文件
    std::ifstream file(zip_path, std::ios::binary);
    if (!file) {
        log::error_fmt("failed to open zip: {}", zip_path.string());
        return false;
    }
    std::vector<uint8_t> data{
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>()};
    file.close();

    if (data.size() < 30) {
        log::error_fmt("zip file too small: {} bytes", data.size());
        return false;
    }

    size_t pos = 0;
    int entry_count = 0;

    while (pos + 30 <= data.size()) {
        uint32_t sig = read_u32_le(&data[pos]);
        if (sig == kCentralDirSig || sig == kEndCentralDirSig) break;
        if (sig != kLocalFileHeaderSig) {
            log::debug_fmt("unexpected signature at offset {}: 0x{:08x}", pos, sig);
            break;
        }

        // Local file header 结构:
        // offset 0:  signature (4)
        // offset 4:  version needed (2)
        // offset 6:  flags (2)
        // offset 8:  compression method (2)
        // offset 10: mod time (2)
        // offset 12: mod date (2)
        // offset 14: crc32 (4)
        // offset 18: compressed size (4)
        // offset 22: uncompressed size (4)
        // offset 26: filename length (2)
        // offset 28: extra field length (2)
        uint16_t flags = read_u16_le(&data[pos + 6]);
        uint16_t method = read_u16_le(&data[pos + 8]);
        uint32_t comp_size = read_u32_le(&data[pos + 18]);
        uint32_t uncomp_size = read_u32_le(&data[pos + 22]);
        uint16_t name_len = read_u16_le(&data[pos + 26]);
        uint16_t extra_len = read_u16_le(&data[pos + 28]);

        // 如果设置了 bit 3 (data descriptor)，从 central directory 获取大小
        // 简化处理：跳过 data descriptor 的条目（不常见于技能包）
        if (flags & 0x08) {
            log::debug_fmt("skipping entry with data descriptor (bit 3 flag)");
            pos += 30 + name_len + extra_len + comp_size;
            if (pos > data.size()) break;
            // 跳过 data descriptor (16 或 24 字节)
            if (pos + 16 <= data.size()) {
                uint32_t dd_sig = read_u32_le(&data[pos]);
                if (dd_sig == 0x08074b50) {
                    pos += 24;  // sig + crc32 + comp + uncomp
                } else {
                    pos += 16;  // crc32 + comp + uncomp (no sig)
                }
            }
            continue;
        }

        size_t name_start = pos + 30;
        if (name_start + name_len > data.size()) break;
        std::string entry_name(reinterpret_cast<const char*>(&data[name_start]), name_len);

        size_t data_start = name_start + name_len + extra_len;
        if (data_start + comp_size > data.size()) {
            log::error_fmt("entry data exceeds file: {}", entry_name);
            break;
        }

        // zip slip 安全检查
        auto out_path = std::filesystem::weakly_canonical(target_dir / entry_name);
        auto canonical_target = std::filesystem::weakly_canonical(target_dir);
        if (out_path.string().find(canonical_target.string()) != 0) {
            log::error_fmt("zip slip detected: {} escapes target dir", entry_name);
            pos = data_start + comp_size;
            continue;
        }

        bool is_dir = (!entry_name.empty() && entry_name.back() == '/');
        if (is_dir) {
            std::filesystem::create_directories(out_path, ec);
            log::debug_fmt("created dir: {}", entry_name);
        } else {
            std::filesystem::create_directories(out_path.parent_path(), ec);

            if (method == kMethodStored) {
                // Stored (不压缩)
                std::ofstream of(out_path, std::ios::binary);
                if (of) {
                    of.write(reinterpret_cast<const char*>(&data[data_start]),
                             static_cast<std::streamsize>(comp_size));
                    log::debug_fmt("extracted (stored): {}", entry_name);
                } else {
                    log::error_fmt("failed to write: {}", out_path.string());
                }
            } else if (method == kMethodDeflated) {
                // Deflated (zlib)
                std::vector<uint8_t> output;
                if (inflate_data(&data[data_start], comp_size, output, uncomp_size)) {
                    std::ofstream of(out_path, std::ios::binary);
                    if (of) {
                        of.write(reinterpret_cast<const char*>(output.data()),
                                 static_cast<std::streamsize>(output.size()));
                        log::debug_fmt("extracted (deflated): {}", entry_name);
                    } else {
                        log::error_fmt("failed to write: {}", out_path.string());
                    }
                } else {
                    log::error_fmt("failed to inflate: {}", entry_name);
                }
            } else {
                log::error_fmt("unsupported compression method {} for: {}", method, entry_name);
            }
        }

        entry_count++;
        pos = data_start + comp_size;
    }

    log::info_fmt("zip extraction complete: {} entries from {}", entry_count, zip_path.string());
    return entry_count > 0;
}

}  // namespace ben_gear::skill
