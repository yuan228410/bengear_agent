#pragma once

/// 跨平台安全随机数生成
///
/// 收敛 BCryptGenRandom / /dev/urandom 差异

#include "platform/os.hpp"

#include <cstdint>
#include <string>

namespace ben_gear::base::platform {

/// 生成安全随机字节
/// @param buf 输出缓冲区
/// @param size 字节数
/// @return 是否成功
bool secure_random_bytes(void* buf, size_t size);

/// 生成安全随机 uint64
uint64_t secure_random_u64();

/// 生成 UUID v4 格式字符串（16 字符 hex）
std::string generate_uuid();

}  // namespace ben_gear::base::platform
