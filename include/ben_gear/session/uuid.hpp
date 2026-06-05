#pragma once

#include "ben_gear/base/container/string.hpp"

#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace ben_gear::session {

namespace container = base::container;

/// 生成 UUID v4（随机）
inline container::String generate_uuid() {
    std::array<unsigned char, 16> buf{};

#ifdef _WIN32
    BCRYPT_ALG_HANDLE h{};
    BCryptOpenAlgorithmProvider(&h, BCRYPT_RNG_ALGORITHM, nullptr, 0);
    BCryptGenRandom(h, buf.data(), 16, 0);
    BCryptCloseAlgorithmProvider(h, 0);
#else
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        auto n = read(fd, buf.data(), 16);
        (void)n;
        close(fd);
    } else {
        // fallback: random_device
        std::random_device rd;
        for (int i = 0; i < 16; i += 4) {
            auto val = rd();
            buf[i] = static_cast<unsigned char>(val);
            buf[i + 1] = static_cast<unsigned char>(val >> 8);
            buf[i + 2] = static_cast<unsigned char>(val >> 16);
            buf[i + 3] = static_cast<unsigned char>(val >> 24);
        }
    }
#endif

    // UUID v4 版本位
    buf[6] = (buf[6] & 0x0F) | 0x40;
    // 变体位
    buf[8] = (buf[8] & 0x3F) | 0x80;

    char out[37];
    std::snprintf(out, sizeof(out),
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        buf[0], buf[1], buf[2], buf[3],
        buf[4], buf[5], buf[6], buf[7],
        buf[8], buf[9], buf[10], buf[11],
        buf[12], buf[13], buf[14], buf[15]);

    return container::String(out);
}

}  // namespace ben_gear::session
