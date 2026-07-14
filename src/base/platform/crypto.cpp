#include "base/platform/crypto.hpp"

#include <cstring>

#if BEN_GEAR_PLATFORM_WINDOWS
#include <windows.h>
#include <bcrypt.h>
#else
#include <mbedtls/sha1.h>
#endif

namespace ben_gear::base::platform {

void sha1(const void* data, size_t size, uint8_t output[20]) {
#if BEN_GEAR_PLATFORM_WINDOWS
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA1_ALGORITHM, nullptr, 0);
    BCryptHash(hAlg, nullptr, 0,
               reinterpret_cast<PUCHAR>(const_cast<void*>(data)),
               static_cast<ULONG>(size), output, 20);
    BCryptCloseAlgorithmProvider(hAlg, 0);
#else
    mbedtls_sha1(reinterpret_cast<const unsigned char*>(data), size, output);
#endif
}

std::string sha1_base64(const std::string& input) {
    uint8_t hash[20];
    sha1(input.data(), input.size(), hash);

    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string r;
    r.reserve(28);
    for (int i = 0; i < 20; i += 3) {
        uint32_t n = static_cast<uint32_t>(hash[i]) << 16;
        if (i + 1 < 20) n |= static_cast<uint32_t>(hash[i + 1]) << 8;
        if (i + 2 < 20) n |= static_cast<uint32_t>(hash[i + 2]);
        r.push_back(b64[(n >> 18) & 0x3F]);
        r.push_back(b64[(n >> 12) & 0x3F]);
        r.push_back((i + 1 < 20) ? b64[(n >> 6) & 0x3F] : '=');
        r.push_back((i + 2 < 20) ? b64[n & 0x3F] : '=');
    }
    return r;
}

}  // namespace ben_gear::base::platform
