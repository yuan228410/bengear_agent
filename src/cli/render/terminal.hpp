#pragma once

#include "cli/render/theme.hpp"
#include "base/container/string.hpp"
#include "base/platform/terminal.hpp"

#include <cstdio>
#include <cstring>
#include <string_view>

namespace ben_gear::cli {

namespace container = base::container;

/// 终端能力检测结果 — 类型别名，统一到 base::platform 层
using base::platform::TerminalCapabilities;

/// 检测终端能力（委托给平台层）
inline TerminalCapabilities detect_terminal() {
    return base::platform::detect_terminal();
}

/// ANSI 转义码生成器（高性能：零堆分配，全部返回固定大小缓冲区）
namespace ansi {

inline container::String esc(const char* code) {
    container::String result;
    result.push_back('\033');
    result.push_back('[');
    result.append(code, std::strlen(code));
    result.push_back('m');
    return result;
}

inline container::String esc_num(int code) {
    container::String result;
    result.push_back('\033');
    result.push_back('[');
    char buf[8];
    int len = 0;
    int n = code;
    if (n == 0) { buf[len++] = '0'; }
    else { while (n > 0) { buf[len++] = '0' + n % 10; n /= 10; } }
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

inline container::String fg(const Color& color, const TerminalCapabilities& cap) {
    if (color.c16 == Color16::none && !color.use_rgb) return {};
    if (color.use_rgb && cap.truecolor) {
        container::String result;
        result.reserve(20);
        result.append("\033[38;2;", 7);
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
    if (color.c16 != Color16::none && cap.color) {
        return esc_num(static_cast<int>(color.c16));
    }
    return {};
}

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
    if (color.c16 != Color16::none && cap.color) {
        return esc_num(static_cast<int>(color.c16) + 10);
    }
    return {};
}

inline container::String style(StyleFlag flags) {
    container::String result;
    if (has_flag(flags, StyleFlag::bold)) { auto s = bold(); result.append(s.data(), s.size()); }
    if (has_flag(flags, StyleFlag::dim)) { auto s = dim(); result.append(s.data(), s.size()); }
    if (has_flag(flags, StyleFlag::italic)) { auto s = italic(); result.append(s.data(), s.size()); }
    if (has_flag(flags, StyleFlag::underline)) { auto s = underline(); result.append(s.data(), s.size()); }
    if (has_flag(flags, StyleFlag::strikethrough)) { auto s = strikethrough(); result.append(s.data(), s.size()); }
    return result;
}

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

inline container::String colorize(std::string_view text,
                                  const Color& fg_color,
                                  StyleFlag flags,
                                  const TerminalCapabilities& cap) {
    if (!cap.is_tty || text.empty()) {
        return container::String(text);
    }
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
