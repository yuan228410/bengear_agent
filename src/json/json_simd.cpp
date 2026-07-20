#include "json/json_simd.hpp"

// 平台检测
#if defined(_M_X64) || defined(__x86_64__)
    #define BENGEAR_JSON_X86 1
    #if defined(__AVX2__) || (defined(_MSC_VER) && defined(__AVX2__))
        #include <immintrin.h>
        #define BENGEAR_JSON_AVX2 1
    #endif
    #if defined(__SSE4_2__) || (defined(_MSC_VER) && defined(__SSE4_2__))
        #include <nmmintrin.h>
        #define BENGEAR_JSON_SSE42 1
    #endif
    // CPUID intrinsics: MSVC 用 <intrin.h>，GCC/Clang 用 <cpuid.h>
    #if defined(_MSC_VER)
        #include <intrin.h>
    #else
        #include <cpuid.h>
    #endif
#elif defined(_M_ARM64) || defined(__aarch64__)
    #define BENGEAR_JSON_ARM 1
    #define BENGEAR_JSON_NEON 1
    #include <arm_neon.h>
#endif

namespace ben_gear::base::json::simd {

// ==================== 标量实现 ====================

namespace scalar {

const char* skip_whitespace(const char* ptr, const char* end) {
    while (ptr < end) {
        char c = *ptr;
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') break;
        ++ptr;
    }
    return ptr;
}

const char* find_char(const char* ptr, const char* end, char target) {
    while (ptr < end) {
        if (*ptr == target) return ptr;
        ++ptr;
    }
    return end;
}

} // namespace scalar

// ==================== SSE4.2 实现 ====================

#if BENGEAR_JSON_SSE42

namespace sse42 {

const char* skip_whitespace(const char* ptr, const char* end) {
    const __m128i ws = _mm_setr_epi8(' ', '\t', '\n', '\r', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    while (ptr + 16 <= end) {
        __m128i chunk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(ptr));
        __m128i cmp = _mm_cmpeq_epi8(chunk, ws);
        int mask = _mm_movemask_epi8(cmp);
        if (mask != 0xFFFF) {
            // 找到第一个非空白
            int pos = __builtin_ctz(~mask);
            return ptr + pos;
        }
        ptr += 16;
    }
    return scalar::skip_whitespace(ptr, end);
}

const char* find_char(const char* ptr, const char* end, char target) {
    __m128i target_vec = _mm_set1_epi8(target);
    while (ptr + 16 <= end) {
        __m128i chunk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(ptr));
        __m128i cmp = _mm_cmpeq_epi8(chunk, target_vec);
        int mask = _mm_movemask_epi8(cmp);
        if (mask != 0) {
            int pos = __builtin_ctz(mask);
            return ptr + pos;
        }
        ptr += 16;
    }
    return scalar::find_char(ptr, end, target);
}

} // namespace sse42

#endif // BENGEAR_JSON_SSE42

// ==================== NEON 实现 ====================

#if BENGEAR_JSON_NEON

namespace neon {

const char* skip_whitespace(const char* ptr, const char* end) {
    const uint8x16_t sp = vdupq_n_u8(' ');
    const uint8x16_t tab = vdupq_n_u8('\t');
    const uint8x16_t nl = vdupq_n_u8('\n');
    const uint8x16_t cr = vdupq_n_u8('\r');
    while (ptr + 16 <= end) {
        uint8x16_t chunk = vld1q_u8(reinterpret_cast<const uint8_t*>(ptr));
        uint8x16_t cmp_sp = vceqq_u8(chunk, sp);
        uint8x16_t cmp_tab = vceqq_u8(chunk, tab);
        uint8x16_t cmp_nl = vceqq_u8(chunk, nl);
        uint8x16_t cmp_cr = vceqq_u8(chunk, cr);
        uint8x16_t any_ws = vorrq_u8(vorrq_u8(cmp_sp, cmp_tab), vorrq_u8(cmp_nl, cmp_cr));
        // any_ws has 0xFF for whitespace bytes, 0x00 for non-whitespace
        // Invert: non-ws bytes become 0xFF
        uint8x16_t non_ws = vmvnq_u8(any_ws);
        uint64_t low = vgetq_lane_u64(vreinterpretq_u64_u8(non_ws), 0);
        uint64_t high = vgetq_lane_u64(vreinterpretq_u64_u8(non_ws), 1);
        if (low != 0) {
            return ptr + (__builtin_ctzll(low) >> 3);
        }
        if (high != 0) {
            return ptr + 8 + (__builtin_ctzll(high) >> 3);
        }
        ptr += 16;
    }
    return scalar::skip_whitespace(ptr, end);
}

const char* find_char(const char* ptr, const char* end, char target) {
    uint8x16_t target_vec = vdupq_n_u8(static_cast<uint8_t>(target));
    while (ptr + 16 <= end) {
        uint8x16_t chunk = vld1q_u8(reinterpret_cast<const uint8_t*>(ptr));
        uint8x16_t cmp = vceqq_u8(chunk, target_vec);
        uint64_t mask = vgetq_lane_u64(vreinterpretq_u64_u8(cmp), 0) |
                        vgetq_lane_u64(vreinterpretq_u64_u8(cmp), 1);
        if (mask != 0) break;
        ptr += 16;
    }
    return scalar::find_char(ptr, end, target);
}

} // namespace neon

#endif // BENGEAR_JSON_NEON

// ==================== 运行时调度 ====================

static SimdOps g_ops;

static void init_ops() {
#if BENGEAR_JSON_SSE42 || BENGEAR_JSON_AVX2 || BENGEAR_JSON_NEON
    Backend backend = detect_backend();
#endif
#if BENGEAR_JSON_SSE42
    if (backend == Backend::SSE42) {
        g_ops.skip_whitespace = sse42::skip_whitespace;
        g_ops.find_char = sse42::find_char;
    } else
#endif
#if BENGEAR_JSON_AVX2
    if (backend == Backend::AVX2) {
        g_ops.skip_whitespace = sse42::skip_whitespace;
        g_ops.find_char = sse42::find_char;
    } else
#endif
#if BENGEAR_JSON_NEON
    if (backend == Backend::NEON) {
        g_ops.skip_whitespace = neon::skip_whitespace;
        g_ops.find_char = neon::find_char;
    } else
#endif
#if BENGEAR_JSON_SSE42 || BENGEAR_JSON_AVX2 || BENGEAR_JSON_NEON
    {
        g_ops.skip_whitespace = scalar::skip_whitespace;
        g_ops.find_char = scalar::find_char;
    }
#else
    g_ops.skip_whitespace = scalar::skip_whitespace;
    g_ops.find_char = scalar::find_char;
#endif
}

Backend detect_backend() {
#if BENGEAR_JSON_X86
    // CPUID 检测
    #if defined(_MSC_VER)
    int cpuinfo[4];
    __cpuid(cpuinfo, 1);
    bool has_sse42 = (cpuinfo[2] & (1 << 20)) != 0;
    #else
    unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
    if (__get_cpuid(1, &eax, &ebx, &ecx, &edx) == 0) {
        return Backend::Scalar;
    }
    bool has_sse42 = (ecx & (1 << 20)) != 0;
    #endif
    if (has_sse42) {
        // 检测 AVX2
        #if defined(_MSC_VER)
        __cpuid(cpuinfo, 7);
        bool has_avx2 = (cpuinfo[1] & (1 << 5)) != 0;
        #else
        eax = ebx = ecx = edx = 0;
        bool has_avx2 = __get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx) != 0
            && (ebx & (1 << 5)) != 0;
        #endif
        if (has_avx2) return Backend::AVX2;
        return Backend::SSE42;
    }
#elif BENGEAR_JSON_ARM
    // ARM 通常都有 NEON
    return Backend::NEON;
#endif
    return Backend::Scalar;
}

const SimdOps& get_ops() {
    static bool initialized = false;
    if (!initialized) {
        init_ops();
        initialized = true;
    }
    return g_ops;
}

} // namespace ben_gear::base::json::simd
