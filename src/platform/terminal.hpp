#pragma once

/// 跨平台终端能力检测
///
/// 收敛 isatty / GetStdHandle / ioctl 等差异

#include "platform/os.hpp"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string_view>

namespace ben_gear::base::platform {

/// 终端能力检测结果
struct TerminalCapabilities {
    bool color = false;
    bool color256 = false;
    bool truecolor = false;
    bool unicode = false;
    int  width = 80;
    int  height = 24;
    bool is_tty = false;
};

/// 检测终端能力（跨平台）
TerminalCapabilities detect_terminal();

/// 检测 stdout 是否是 TTY
inline bool is_stdout_tty() {
#if BEN_GEAR_PLATFORM_WINDOWS
    return _isatty(_fileno(stdout)) != 0;
#else
    return isatty(STDOUT_FILENO) != 0;
#endif
}

/// 检测 stderr 是否是 TTY
inline bool is_stderr_tty() {
#if BEN_GEAR_PLATFORM_WINDOWS
    return _isatty(_fileno(stderr)) != 0;
#else
    return isatty(STDERR_FILENO) != 0;
#endif
}

/// 启用 Windows 终端 VT 处理（ANSI 转义码支持）
/// 其他平台无操作
inline void enable_vt_processing() {
#if BEN_GEAR_PLATFORM_WINDOWS
    auto* h = ::GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (::GetConsoleMode(h, &mode)) {
        ::SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
#endif
}

}  // namespace ben_gear::base::platform
