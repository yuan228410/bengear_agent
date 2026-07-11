#pragma once

#include <atomic>
#include <thread>

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
        for (int i = 0; ; ++i) {
            if (i < 4) {
                // 阶段1：忙等 + CPU PAUSE 提示
#if defined(_M_X86) || defined(__x86_64__)
                __builtin_ia32_pause();
#elif defined(_M_ARM) || defined(__aarch64__)
                __asm__ __volatile__("yield" ::: "memory");
#else
                // 其他平台无 PAUSE 指令
#endif
            } else if (i < 16) {
                // 阶段2：让出 CPU 时间片
                std::this_thread::yield();
            } else {
                // 阶段3：短暂睡眠，避免浪费 CPU
                std::this_thread::sleep_for(std::chrono::microseconds(50));
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
