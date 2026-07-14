#pragma once

/// 跨平台 crash handler 框架
///
/// 设计：
/// - base/platform 提供通用 crash handler（注册信号、平台栈回溯）
/// - 上层通过 callback 注入清理逻辑（如恢复终端状态）
/// - 避免 base 层反向依赖 cli 层

#include "base/platform/os.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>

#ifdef _WIN32
#ifdef _MSC_VER
#pragma comment(lib, "dbghelp.lib")
#endif
#include <windows.h>
#include <dbghelp.h>
#include <csignal>
#else
#include <dlfcn.h>
#include <execinfo.h>
#include <unistd.h>
#endif

namespace ben_gear::base::platform {

/// crash 时的回调类型（上层注入清理逻辑）
using CrashCallback = std::function<void()>;

/// 注册 crash 回调（在 crash handler 执行前调用）
void register_crash_callback(CrashCallback cb);

/// 安装跨平台 crash handler
/// @param cb crash 时调用的回调（如恢复终端），可为空
void install_crash_handler(CrashCallback cb = nullptr);

/// 获取信号名称（跨平台）
inline const char* signal_name(int sig) {
#if defined(SIGSEGV) && defined(SIGABRT) && defined(SIGILL) && defined(SIGFPE)
    switch (sig) {
#ifdef SIGBUS
        case SIGBUS:  return "SIGBUS";
#endif
        case SIGSEGV: return "SIGSEGV";
        case SIGABRT: return "SIGABRT";
        case SIGILL:  return "SIGILL";
        case SIGFPE:  return "SIGFPE";
        default:      return "UNKNOWN";
    }
#else
    (void)sig;
    return "UNKNOWN";
#endif
}

}  // namespace ben_gear::base::platform
