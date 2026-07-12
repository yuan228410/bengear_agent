#include "base/platform/random.hpp"

#include <cstdio>
#include <random>

#if BEN_GEAR_PLATFORM_WINDOWS
#include <windows.h>
#include <bcrypt.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace ben_gear::base::platform {

bool secure_random_bytes(void* buf, size_t size) {
#if BEN_GEAR_PLATFORM_WINDOWS
    BCRYPT_ALG_HANDLE h{};
    if (BCryptOpenAlgorithmProvider(&h, BCRYPT_RNG_ALGORITHM, nullptr, 0) < 0) {
        return false;
    }
    NTSTATUS status = BCryptGenRandom(h, reinterpret_cast<PUCHAR>(buf),
                                       static_cast<ULONG>(size), 0);
    BCryptCloseAlgorithmProvider(h, 0);
    return status >= 0;
#else
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        size_t total = 0;
        while (total < size) {
            auto n = read(fd, static_cast<char*>(buf) + total, size - total);
            if (n <= 0) break;
            total += n;
        }
        close(fd);
        return total == size;
    }
    // fallback: std::random_device
    std::random_device rd;
    auto* p = static_cast<uint8_t*>(buf);
    for (size_t i = 0; i < size; ++i) {
        p[i] = static_cast<uint8_t>(rd());
    }
    return true;
#endif
}

uint64_t secure_random_u64() {
    uint64_t val = 0;
    secure_random_bytes(&val, sizeof(val));
    return val;
}

std::string generate_uuid() {
    uint64_t val = secure_random_u64();
    char out[17];
    std::snprintf(out, sizeof(out), "%016llx",
                  static_cast<unsigned long long>(val));
    return std::string(out);
}

}  // namespace ben_gear::base::platform
