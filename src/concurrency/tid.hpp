#pragma once

/// 跨平台线程 ID 获取
///
/// 收敛 GetCurrentThreadId / syscall(SYS_gettid) / pthread_threadid_np 差异
/// 注意：这是系统原生线程 ID（用于调试），不同于 base/platform/os.hpp 中的逻辑线程 ID

#include "platform/os.hpp"

#include <cstdint>

#ifdef __linux__
#include <sys/syscall.h>
#endif

namespace ben_gear::base::concurrency {

/// 获取当前线程的系统原生 ID（跨平台）
inline uint64_t current_thread_id() {
#if BEN_GEAR_PLATFORM_WINDOWS
    return static_cast<uint64_t>(::GetCurrentThreadId());
#elif defined(__linux__)
    return static_cast<uint64_t>(::syscall(SYS_gettid));
#elif defined(__APPLE__)
    uint64_t tid = 0;
    ::pthread_threadid_np(nullptr, &tid);
    return tid;
#else
    return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(::pthread_self()));
#endif
}

} // namespace ben_gear::base::concurrency
