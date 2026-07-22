#pragma once
#include <chrono>

#include <atomic>
#include <thread>

#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace ben_gear::base::concurrency {

/// 自旋锁（轻量级互斥）
/// 适合临界区极短的场景（如内存池 free list 操作）
/// 特性：
/// - 无内核态切换，纯用户态自旋
/// - 指数退避：先忙等 → PAUSE → yield → sleep
/// - 跨平台：使用 std::atomic_flag
class Spinlock {
public:
    Spinlock() = default;

    void lock() {
        // 快速路径：无竞争直接获取
        if (!flag_.test_and_set(std::memory_order_acquire)) {
            return;
        }
        // 慢速路径：指数退避
        // 临界区通常 ~10ns，所以前 128 次 PAUSE（~1.28μs）纯用户态自旋，不进入内核
        // 之后 yield 64 次，最后才 sleep，避免上下文切换抖动
        for (int i = 0; ; ++i) {
            if (i < 128) {
                // 阶段1：纯用户态 PAUSE 退避，约 1.28μs 内不进入内核
#if defined(__x86_64__) || defined(_M_X64) || defined(_M_IX86) || defined(_M_X86)
#if defined(_MSC_VER)
                _mm_pause();
#else
                __builtin_ia32_pause();
#endif
#elif defined(__aarch64__) || defined(_M_ARM64)
                __asm__ __volatile__("yield" ::: "memory");
#elif defined(_M_ARM)
                __dmb(0);
#endif
            } else if (i < 192) {
                // 阶段2：让出 CPU 时间片
                std::this_thread::yield();
            } else {
                // 阶段3：短暂睡眠，避免浪费 CPU
                std::this_thread::sleep_for(std::chrono::microseconds(500));
            }
            if (!flag_.test_and_set(std::memory_order_acquire)) {
                return;
            }
        }
    }

    bool try_lock() {
        return !flag_.test_and_set(std::memory_order_acquire);
    }

    void unlock() {
        flag_.clear(std::memory_order_release);
    }

private:
    std::atomic_flag flag_{};
};

/// RAII 自旋锁守卫
class SpinlockGuard {
public:
    explicit SpinlockGuard(Spinlock& lock) : lock_(lock) { lock_.lock(); }
    ~SpinlockGuard() { lock_.unlock(); }
    SpinlockGuard(const SpinlockGuard&) = delete;
    SpinlockGuard& operator=(const SpinlockGuard&) = delete;

private:
    Spinlock& lock_;
};

}  // namespace ben_gear::base::concurrency
