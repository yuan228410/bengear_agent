#pragma once

#include <cstddef>

namespace ben_gear::base::json::simd {

/// SIMD 后端类型
enum class Backend {
    Scalar,   // 标量回退（所有平台）
    SSE42,    // x86 SSE4.2
    AVX2,     // x86 AVX2
    NEON      // ARM NEON
};

/// SIMD 操作接口
struct SimdOps {
    // 跳过空白字符，返回首个非空白位置
    const char* (*skip_whitespace)(const char* ptr, const char* end) = nullptr;

    // 查找指定字符（如引号、反斜杠）
    const char* (*find_char)(const char* ptr, const char* end, char target) = nullptr;
};

/// 检测当前平台最优后端
Backend detect_backend();

/// 获取当前平台最优操作集
const SimdOps& get_ops();

/// 标量实现（所有平台的回退）
namespace scalar {

const char* skip_whitespace(const char* ptr, const char* end);
const char* find_char(const char* ptr, const char* end, char target);

} // namespace scalar

} // namespace ben_gear::base::json::simd
