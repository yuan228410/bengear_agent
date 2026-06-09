#pragma once

#include "ben_gear/cli/render/theme.hpp"
#include "ben_gear/base/container/string.hpp"

#include <cstdio>
#include <cstring>
#include <string_view>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <io.h>
#define STDOUT_FILENO 1
#define STDERR_FILENO 2
#else
#include <unistd.h>
#include <sys/ioctl.h>
#endif

namespace ben_gear::cli {

namespace container = base::container;

/// 终端能力检测结果（只检测一次，传递给各组件）
struct TerminalCapabilities {
    bool color = false;
    bool color256 = false;
    bool truecolor = false;
    bool unicode = false;
    int  width = 80;
    int  height = 24;
    bool is_tty = false;

    static TerminalCapabilities detect() {
        TerminalCapabilities cap;
#ifdef _WIN32
        cap.is_tty = _isatty(STDOUT_FILENO) && _isatty(STDERR_FILENO);
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

#ifdef _WIN32
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
        if (lang && (strstr(lang, "UTF-8") || strstr(lang, "utf8") ||
                     strstr(lang, "UTF-8"))) {
            cap.unicode = true;
        }
        // TERM_PROGRAM 检测（macOS 终端）
        const char* term_program = std::getenv("TERM_PROGRAM");
        if (term_program) cap.unicode = true;
#endif

        return cap;
    }
};

/// ANSI 转义码生成器（高性能：零堆分配，全部返回固定大小缓冲区）
///
/// 核心优化：
/// - 所有函数返回 container::String，SSO 覆盖大部分场景（ANSI 码 < 23 字节）
/// - 终端不支持颜色时直接返回空串，零开销降级
/// - 不使用 std::format 等重量级格式化
namespace ansi {

/// 内联辅助：构造 ANSI 转义码
inline container::String esc(const char* code) {
    container::String result;
    result.push_back('\033');
    result.push_back('[');
    result.append(code, std::strlen(code));
    result.push_back('m');
    return result;
}

/// 内联辅助：构造 ANSI 转义码（数字参数）
inline container::String esc_num(int code) {
    container::String result;
    result.push_back('\033');
    result.push_back('[');
    // 数字转字符串（不用 snprintf，避免堆分配）
    char buf[8];
    int len = 0;
    int n = code;
    if (n == 0) { buf[len++] = '0'; }
    else { while (n > 0) { buf[len++] = '0' + n % 10; n /= 10; } }
    // 反转
    for (int i = 0; i < len / 2; ++i) { char t = buf[i]; buf[i] = buf[len-1-i]; buf[len-1-i] = t; }
    result.append(buf, static_cast<size_t>(len));
    result.push_back('m');
    return result;
}

inline container::String reset() { return esc("0"); }
inline container::String bold() { return esc_num(1); }
inline container::String dim() { return esc_num(2); }
inline container::String italic() { return esc_num(3); }
inline container::String underline() { return esc_num(4); }
inline container::String strikethrough() { return esc_num(9); }

/// 前景色（根据终端能力选择16色/真彩色）
inline container::String fg(const Color& color, const TerminalCapabilities& cap) {
    if (color.c16 == Color16::none && !color.use_rgb) return {};
    if (color.use_rgb && cap.truecolor) {
        // \033[38;2;R;G;Bm
        container::String result;
        result.reserve(20);
        result.append("\033[38;2;", 7);
        // RGB 数值转字符串（内联，避免 snprintf）
        auto append_num = [](container::String& s, int n) {
            char buf[4]; int len = 0;
            if (n == 0) { buf[len++] = '0'; }
            else { while (n > 0) { buf[len++] = '0' + n % 10; n /= 10; } }
            for (int i = 0; i < len / 2; ++i) { char t = buf[i]; buf[i] = buf[len-1-i]; buf[len-1-i] = t; }
            s.append(buf, static_cast<size_t>(len));
        };
        append_num(result, color.rgb.r);
        result.push_back(';');
        append_num(result, color.rgb.g);
        result.push_back(';');
        append_num(result, color.rgb.b);
        result.push_back('m');
        return result;
    }
    // 16色
    if (color.c16 != Color16::none && cap.color) {
        return esc_num(static_cast<int>(color.c16));
    }
    return {};
}

/// 背景色
inline container::String bg(const Color& color, const TerminalCapabilities& cap) {
    if (color.c16 == Color16::none && !color.use_rgb) return {};
    if (color.use_rgb && cap.truecolor) {
        container::String result;
        result.reserve(20);
        result.append("\033[48;2;", 7);
        auto append_num = [](container::String& s, int n) {
            char buf[4]; int len = 0;
            if (n == 0) { buf[len++] = '0'; }
            else { while (n > 0) { buf[len++] = '0' + n % 10; n /= 10; } }
            for (int i = 0; i < len / 2; ++i) { char t = buf[i]; buf[i] = buf[len-1-i]; buf[len-1-i] = t; }
            s.append(buf, static_cast<size_t>(len));
        };
        append_num(result, color.rgb.r);
        result.push_back(';');
        append_num(result, color.rgb.g);
        result.push_back(';');
        append_num(result, color.rgb.b);
        result.push_back('m');
        return result;
    }
    // 16色背景码 = 前景码 + 10
    if (color.c16 != Color16::none && cap.color) {
        return esc_num(static_cast<int>(color.c16) + 10);
    }
    return {};
}

/// 应用样式
inline container::String style(StyleFlag flags) {
    container::String result;
    if (has_flag(flags, StyleFlag::bold)) { auto s = bold(); result.append(s.data(), s.size()); }
    if (has_flag(flags, StyleFlag::dim)) { auto s = dim(); result.append(s.data(), s.size()); }
    if (has_flag(flags, StyleFlag::italic)) { auto s = italic(); result.append(s.data(), s.size()); }
    if (has_flag(flags, StyleFlag::underline)) { auto s = underline(); result.append(s.data(), s.size()); }
    if (has_flag(flags, StyleFlag::strikethrough)) { auto s = strikethrough(); result.append(s.data(), s.size()); }
    return result;
}

/// 光标控制（不依赖颜色能力，只依赖 is_tty）
inline container::String cursor_up(int n) {
    if (n <= 0) return {};
    container::String result;
    result.push_back('\033');
    result.push_back('[');
    char buf[8]; int len = 0;
    int v = n; while (v > 0) { buf[len++] = '0' + v % 10; v /= 10; }
    for (int i = 0; i < len / 2; ++i) { char t = buf[i]; buf[i] = buf[len-1-i]; buf[len-1-i] = t; }
    result.append(buf, static_cast<size_t>(len));
    result.push_back('A');
    return result;
}

inline container::String clear_line() {
    container::String result;
    result.append("\033[2K\r", 5);
    return result;
}

inline container::String hide_cursor() {
    container::String result;
    result.append("\033[?25l", 6);
    return result;
}

inline container::String show_cursor() {
    container::String result;
    result.append("\033[?25h", 6);
    return result;
}

/// 着色一段文本并自动 reset（高性能：预计算总长度，一次分配）
inline container::String colorize(std::string_view text,
                                  const Color& fg_color,
                                  StyleFlag flags,
                                  const TerminalCapabilities& cap) {
    if (!cap.is_tty || text.empty()) {
        return container::String(text);
    }

    // 预估：前缀 + 文本 + reset，ANSI 码最长 ~30 字节
    container::String result;
    result.reserve(text.size() + 64);

    auto f = fg(fg_color, cap);
    auto s = style(flags);
    if (!f.empty()) result.append(f.data(), f.size());
    if (!s.empty()) result.append(s.data(), s.size());

    result.append(text.data(), text.size());

    auto r = reset();
    result.append(r.data(), r.size());
    return result;
}

/// 着色文本（前景+背景+样式）
inline container::String colorize(std::string_view text,
                                  const Color& fg_color,
                                  const Color& bg_color,
                                  StyleFlag flags,
                                  const TerminalCapabilities& cap) {
    if (!cap.is_tty || text.empty()) {
        return container::String(text);
    }

    container::String result;
    result.reserve(text.size() + 96);

    auto f = fg(fg_color, cap);
    auto b = bg(bg_color, cap);
    auto s = style(flags);
    if (!f.empty()) result.append(f.data(), f.size());
    if (!b.empty()) result.append(b.data(), b.size());
    if (!s.empty()) result.append(s.data(), s.size());

    result.append(text.data(), text.size());

    auto r = reset();
    result.append(r.data(), r.size());
    return result;
}

}  // namespace ansi

}  // namespace ben_gear::cli
