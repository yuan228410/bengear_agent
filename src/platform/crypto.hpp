#pragma once

/// 跨平台加密原语
///
/// 收敛 BCrypt / mbedtls 差异

#include "platform/os.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace ben_gear::base::platform {

/// SHA-1 哈希（20 字节输出）
/// 用于 WebSocket handshake (RFC 6455)
void sha1(const void* data, size_t size, uint8_t output[20]);

/// SHA-1 Base64 编码（WebSocket Sec-WebSocket-Accept）
std::string sha1_base64(const std::string& input);

}  // namespace ben_gear::base::platform
