#include "base/platform/terminal.hpp"

namespace ben_gear::base::platform {

TerminalCapabilities detect_terminal() {
    TerminalCapabilities cap;

#if BEN_GEAR_PLATFORM_WINDOWS
    cap.is_tty = _isatty(_fileno(stdout)) && _isatty(_fileno(stderr));
    // Windows 终端尺寸
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h != INVALID_HANDLE_VALUE) {
        CONSOLE_SCREEN_BUFFER_INFO csbi{};
        if (GetConsoleScreenBufferInfo(h, &csbi)) {
            cap.width = static_cast<int>(csbi.srWindow.Right - csbi.srWindow.Left + 1);
            cap.height = static_cast<int>(csbi.srWindow.Bottom - csbi.srWindow.Top + 1);
        }
    }
#else
    cap.is_tty = isatty(STDOUT_FILENO) && isatty(STDERR_FILENO);
    // POSIX 终端尺寸
    struct winsize ws{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) {
        cap.width = static_cast<int>(ws.ws_col);
        cap.height = static_cast<int>(ws.ws_row);
    }
#endif

    if (!cap.is_tty) return cap;

    // NO_COLOR 优先级最高
    if (std::getenv("NO_COLOR") != nullptr) return cap;

#if BEN_GEAR_PLATFORM_WINDOWS
    // Windows Terminal / ConEmu 默认支持真彩色和 Unicode
    cap.color = true;
    cap.color256 = true;
    cap.truecolor = true;
    // Windows 10+ 默认支持 Unicode
    cap.unicode = true;
#else
    // TERM 检测
    const char* term = std::getenv("TERM");
    if (!term) return cap;

    // 基本颜色支持
    cap.color = (strcmp(term, "dumb") != 0);

    // 256色
    const char* colorterm = std::getenv("COLORTERM");
    cap.color256 = cap.color;
    if (term && (strstr(term, "256color") || strstr(term, "xterm"))) {
        cap.color256 = true;
    }

    // 真彩色
    if (colorterm && (strcmp(colorterm, "truecolor") == 0 ||
                      strcmp(colorterm, "24bit") == 0)) {
        cap.truecolor = true;
    }

    // Unicode
    const char* lang = std::getenv("LANG");
    if (lang && (strstr(lang, "UTF-8") || strstr(lang, "utf8"))) {
        cap.unicode = true;
    }
    // TERM_PROGRAM 检测（macOS 终端）
    const char* term_program = std::getenv("TERM_PROGRAM");
    if (term_program) cap.unicode = true;
#endif

    return cap;
}

}  // namespace ben_gear::base::platform
