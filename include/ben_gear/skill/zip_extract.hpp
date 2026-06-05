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
inline bool download_file(const std::string& url, const std::filesystem::path& dest, bool expect_zip = false) {
    log::info_fmt("downloading: {} -> {}", url, dest.string());

    // 1. 优先使用自有 HttpClient
    try {
        net::HttpClient client;
        auto resp = client.get(url, {});
        if (resp.status == 200 && !resp.body.empty()) {
            // 校验内容：如果期望 zip，检查 magic number (PK\x03\x04)
            if (expect_zip && resp.body.size() >= 4) {
                auto* p = reinterpret_cast<const unsigned char*>(resp.body.data());
                if (p[0] != 0x50 || p[1] != 0x4B || p[2] != 0x03 || p[3] != 0x04) {
                    log::warn_fmt("HttpClient downloaded non-zip content ({} bytes, magic={:02x}{:02x}{:02x}{:02x}), likely CDN error response",
                                  resp.body.size(), p[0], p[1], p[2], p[3]);
                    // 不写入文件，走 curl 回退
                    goto try_curl;
                }
            }
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

try_curl:
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

    // 校验 zip magic number，防止误传 JSON/HTML 错误响应
    {
        char magic[4] = {};
        file.read(magic, 4);
        auto n = file.gcount();
        auto m0 = static_cast<unsigned char>(magic[0]);
        auto m1 = static_cast<unsigned char>(magic[1]);
        auto m2 = static_cast<unsigned char>(magic[2]);
        auto m3 = static_cast<unsigned char>(magic[3]);
        if (n < 4 || m0 != 0x50 || m1 != 0x4B || m2 != 0x03 || m3 != 0x04) {
            log::error_fmt("not a valid zip file: {} (magic={:02x}{:02x}{:02x}{:02x}), "
                           "downloaded content may be a CDN error response",
                           zip_path.string(), m0, m1, m2, m3);
            return false;
        }
        file.seekg(0);
    }
    std::vector<uint8_t> data{
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>()};
    file.close();

    if (data.size() < 30) {
        log::error_fmt("zip file too small: {} bytes", data.size());
        return false;
    }

    // 预解析 central directory，构建文件名 → 大小的映射
    // 用于处理 data descriptor 条目（bit 3 flag，local header 中大小为 0）
    struct CdEntry { uint32_t comp_size; uint32_t uncomp_size; };
    std::unordered_map<std::string, CdEntry> cd_map;
    {
        // 找 End of Central Directory Record
        auto eocd_pos = data.size();
        for (size_t i = data.size(); i >= 22; --i) {
            if (read_u32_le(&data[i - 4]) == kEndCentralDirSig) {
                eocd_pos = i - 4;
                break;
            }
        }
        if (eocd_pos < data.size()) {
            uint16_t cd_entries = read_u16_le(&data[eocd_pos + 10]);
            uint32_t cd_offset = read_u32_le(&data[eocd_pos + 16]);
            size_t cd_pos = cd_offset;
            for (uint16_t i = 0; i < cd_entries && cd_pos + 46 <= data.size(); ++i) {
                if (read_u32_le(&data[cd_pos]) != kCentralDirSig) break;
                uint32_t cd_comp = read_u32_le(&data[cd_pos + 20]);
                uint32_t cd_uncomp = read_u32_le(&data[cd_pos + 24]);
                uint16_t cd_name_len = read_u16_le(&data[cd_pos + 28]);
                uint16_t cd_extra_len = read_u16_le(&data[cd_pos + 30]);
                uint16_t cd_comment_len = read_u16_le(&data[cd_pos + 32]);
                if (cd_pos + 46 + cd_name_len <= data.size()) {
                    std::string cd_name(reinterpret_cast<const char*>(&data[cd_pos + 46]), cd_name_len);
                    cd_map[cd_name] = CdEntry{cd_comp, cd_uncomp};
                }
                cd_pos += 46 + cd_name_len + cd_extra_len + cd_comment_len;
            }
        }
        log::info_fmt("zip central directory: {} entries mapped", cd_map.size());
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

        // bit 3 (data descriptor): local header 中 comp_size/uncomp_size 可能为 0
        // 需要从 central directory 获取真实大小
        if (flags & 0x08) {
            // 从预构建的 cd_map_ 中查找
            size_t name_start_tmp = pos + 30;
            if (name_start_tmp + name_len <= data.size()) {
                std::string tmp_name(reinterpret_cast<const char*>(&data[name_start_tmp]), name_len);
                auto it = cd_map.find(tmp_name);
                if (it != cd_map.end()) {
                    comp_size = it->second.comp_size;
                    uncomp_size = it->second.uncomp_size;
                    log::debug_fmt("data descriptor entry '{}': using cd sizes comp={} uncomp={}",
                                   tmp_name, comp_size, uncomp_size);
                } else {
                    log::warn_fmt("data descriptor entry '{}' not found in central directory, skipping", tmp_name);
                    // 无法确定大小，跳过
                    break;
                }
            }
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
        // data descriptor 条目：跳过 trailing data descriptor
        if (flags & 0x08) {
            if (pos + 24 <= data.size()) {
                uint32_t dd_sig = read_u32_le(&data[pos]);
                pos += (dd_sig == 0x08074b50) ? 16 : 12;
            }
        }
    }

    log::info_fmt("zip extraction complete: {} entries from {}", entry_count, zip_path.string());
    return entry_count > 0;
}

}  // namespace ben_gear::skill
