#pragma once

#include "ben_gear/base/container/string.hpp"

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

namespace ben_gear::workspace {

namespace container = base::container;

/// 生成会话 ID（16 位十六进制，碰撞概率约 10^19 分之一）
inline container::String generate_uuid() {
    uint64_t val = 0;

#ifdef _WIN32
    BCRYPT_ALG_HANDLE h{};
    BCryptOpenAlgorithmProvider(&h, BCRYPT_RNG_ALGORITHM, nullptr, 0);
    BCryptGenRandom(h, reinterpret_cast<PUCHAR>(&val), 8, 0);
    BCryptCloseAlgorithmProvider(h, 0);
#else
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        auto n = read(fd, &val, 8);
        (void)n;
        close(fd);
    } else {
        std::random_device rd;
        val = (static_cast<uint64_t>(rd()) << 32) | rd();
    }
#endif

    char out[17];
    std::snprintf(out, sizeof(out), "%016llx", static_cast<unsigned long long>(val));
    return container::String(out);
}

}  // namespace ben_gear::workspace
