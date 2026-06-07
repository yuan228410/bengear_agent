#pragma once
#include <cstdint>
#ifdef _WIN32
#include <processthreadsapi.h>
#else
#include <sys/syscall.h>
#include <unistd.h>
#include <pthread.h>
#endif

namespace ben_gear::base::concurrency {

/// 获取当前线程的系统原生 ID（跨平台）
inline uint64_t current_thread_id() {
#ifdef _WIN32
    return static_cast<uint64_t>(GetCurrentThreadId());
#elif defined(__linux__)
    return static_cast<uint64_t>(syscall(SYS_gettid));
#elif defined(__APPLE__)
    uint64_t tid = 0;
    pthread_threadid_np(nullptr, &tid);
    return tid;
#else
    return static_cast<uint64_t>(pthread_self());
#endif
}

} // namespace ben_gear::base::concurrency
