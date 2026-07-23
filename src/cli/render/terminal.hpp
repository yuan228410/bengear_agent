#pragma once

#include "cli/render/theme.hpp"
#include "platform/terminal.hpp"

#include <string_view>

namespace ben_gear::cli {


/// 终端能力检测结果 — 类型别名，统一到 base::platform 层
using base::platform::TerminalCapabilities;

/// 检测终端能力（委托给平台层）
inline TerminalCapabilities detect_terminal() {
    return base::platform::detect_terminal();
}

/// ANSI 转义码生成器（高性能：静态缓存 + 预计算）
namespace ansi {

inline std::string esc(const char* code) {
    std::string result;
    result.push_back('\033');
    result.push_back('[');
    result.append(code, std::strlen(code));
    result.push_back('m');
    return result;
}

inline std::string esc_num(int code) {
    std::string result;
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

// 固定 ANSI 码：static const 引用，零分配零拷贝
inline const std::string& reset() { static const std::string s = esc("0"); return s; }
inline const std::string& bold() { static const std::string s = esc_num(1); return s; }
inline const std::string& dim() { static const std::string s = esc_num(2); return s; }
inline const std::string& italic() { static const std::string s = esc_num(3); return s; }
inline const std::string& underline() { static const std::string s = esc_num(4); return s; }
inline const std::string& strikethrough() { static const std::string s = esc_num(9); return s; }
inline const std::string& clear_line() { static const std::string s = []{ std::string r; r.append("\033[2K\r", 5); return r; }(); return s; }
inline const std::string& hide_cursor() { static const std::string s = []{ std::string r; r.append("\033[?25l", 6); return r; }(); return s; }
inline const std::string& show_cursor() { static const std::string s = []{ std::string r; r.append("\033[?25h", 6); return r; }(); return s; }
inline const std::string& clear_screen() { static const std::string s = esc("2J") + esc("H"); return s; }

inline std::string fg(const Color& color, const TerminalCapabilities& cap) {
    if (color.c16 == Color16::none && !color.use_rgb) return {};
    if (color.use_rgb && cap.truecolor) {
        std::string result;
        result.reserve(20);
        result.append("\033[38;2;", 7);
        auto append_num = [](std::string& s, int n) {
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

inline std::string bg(const Color& color, const TerminalCapabilities& cap) {
    if (color.c16 == Color16::none && !color.use_rgb) return {};
    if (color.use_rgb && cap.truecolor) {
        std::string result;
        result.reserve(20);
        result.append("\033[48;2;", 7);
        auto append_num = [](std::string& s, int n) {
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

inline std::string style(StyleFlag flags) {
    std::string result;
    if (has_flag(flags, StyleFlag::bold)) { auto& s = bold(); result.append(s.data(), s.size()); }
    if (has_flag(flags, StyleFlag::dim)) { auto& s = dim(); result.append(s.data(), s.size()); }
    if (has_flag(flags, StyleFlag::italic)) { auto& s = italic(); result.append(s.data(), s.size()); }
    if (has_flag(flags, StyleFlag::underline)) { auto& s = underline(); result.append(s.data(), s.size()); }
    if (has_flag(flags, StyleFlag::strikethrough)) { auto& s = strikethrough(); result.append(s.data(), s.size()); }
    return result;
}

inline std::string cursor_up(int n) {
    if (n <= 0) return {};
    std::string result;
    result.push_back('\033');
    result.push_back('[');
    char buf[8]; int len = 0;
    int v = n; while (v > 0) { buf[len++] = '0' + v % 10; v /= 10; }
    for (int i = 0; i < len / 2; ++i) { char t = buf[i]; buf[i] = buf[len-1-i]; buf[len-1-i] = t; }
    result.append(buf, static_cast<size_t>(len));
    result.push_back('A');
    return result;
}

}  // namespace ansi

// ==================== ANSI 样式缓存（构造时预计算，运行时零分配） ====================

/// 预计算所有主题色的 fg/bg ANSI 码，避免热路径重复分配
struct AnsiStyleCache {
    // ---- 常用样式（指向 ansi:: 静态常量，零开销） ----
    const std::string& reset{ansi::reset()};
    const std::string& bold{ansi::bold()};
    const std::string& dim{ansi::dim()};
    const std::string& italic{ansi::italic()};
    const std::string& underline{ansi::underline()};
    const std::string& strikethrough{ansi::strikethrough()};
    const std::string& clear_line{ansi::clear_line()};
    const std::string& hide_cursor{ansi::hide_cursor()};
    const std::string& show_cursor{ansi::show_cursor()};

    // ---- 预计算的主题色（构造时分配一次，后续零开销） ----
    std::string assistant_text;
    std::string thinking_label;
    std::string thinking_text;
    std::string tool_name;
    std::string tool_args;
    std::string tool_success_marker;
    std::string tool_error_marker;
    std::string tool_error_text;
    std::string system_info;
    std::string error_text;
    std::string assistant_heading_h1;
    std::string assistant_heading_h2;
    std::string assistant_heading_h3;
    std::string assistant_code_bg;
    std::string assistant_code_text;
    std::string assistant_code_lang;
    std::string assistant_hr;
    std::string assistant_blockquote_border;
    std::string assistant_blockquote_text;
    std::string assistant_list_marker;
    std::string assistant_table_border;
    std::string assistant_table_header;
    std::string assistant_inline_code_bg;
    std::string assistant_inline_code_text;
    std::string assistant_link;

    AnsiStyleCache(const Theme& theme, const TerminalCapabilities& cap) {
        assistant_text           = ansi::fg(theme.assistant_text, cap);
        thinking_label           = ansi::fg(theme.thinking_label, cap);
        thinking_text            = ansi::fg(theme.thinking_text, cap);
        tool_name                = ansi::fg(theme.tool_name, cap);
        tool_args                = ansi::fg(theme.tool_args, cap);
        tool_success_marker      = ansi::fg(theme.tool_success_marker, cap);
        tool_error_marker        = ansi::fg(theme.tool_error_marker, cap);
        tool_error_text          = ansi::fg(theme.tool_error_text, cap);
        system_info              = ansi::fg(theme.system_info, cap);
        error_text               = ansi::fg(theme.error_text, cap);
        assistant_heading_h1     = ansi::fg(theme.assistant_heading_h1, cap);
        assistant_heading_h2     = ansi::fg(theme.assistant_heading_h2, cap);
        assistant_heading_h3     = ansi::fg(theme.assistant_heading_h3, cap);
        assistant_code_bg        = ansi::bg(theme.assistant_code_bg, cap);
        assistant_code_text      = ansi::fg(theme.assistant_code_text, cap);
        assistant_code_lang      = ansi::fg(theme.assistant_code_lang, cap);
        assistant_hr             = ansi::fg(theme.assistant_hr, cap);
        assistant_blockquote_border = ansi::fg(theme.assistant_blockquote_border, cap);
        assistant_blockquote_text   = ansi::fg(theme.assistant_blockquote_text, cap);
        assistant_list_marker       = ansi::fg(theme.assistant_list_marker, cap);
        assistant_table_border      = ansi::fg(theme.assistant_table_border, cap);
        assistant_table_header      = ansi::fg(theme.assistant_table_header, cap);
        assistant_inline_code_bg    = ansi::bg(theme.assistant_inline_code_bg, cap);
        assistant_inline_code_text  = ansi::fg(theme.assistant_inline_code_text, cap);
        assistant_link              = ansi::fg(theme.assistant_link, cap);
    }
};

// ---- colorize 重载：接受预缓存的 fg/bg 字符串（零分配热路径） ----

inline std::string colorize_cached(std::string_view text,
                                   const std::string& fg_cache,
                                   const std::string& bg_cache,
                                   StyleFlag flags,
                                   const AnsiStyleCache& cache) {
    if (text.empty()) return std::string(text);
    std::string result;
    result.reserve(text.size() + 64);
    if (!fg_cache.empty()) result.append(fg_cache.data(), fg_cache.size());
    if (!bg_cache.empty()) result.append(bg_cache.data(), bg_cache.size());
    if (has_flag(flags, StyleFlag::bold)) result.append(cache.bold.data(), cache.bold.size());
    if (has_flag(flags, StyleFlag::dim)) result.append(cache.dim.data(), cache.dim.size());
    if (has_flag(flags, StyleFlag::italic)) result.append(cache.italic.data(), cache.italic.size());
    if (has_flag(flags, StyleFlag::underline)) result.append(cache.underline.data(), cache.underline.size());
    if (has_flag(flags, StyleFlag::strikethrough)) result.append(cache.strikethrough.data(), cache.strikethrough.size());
    result.append(text.data(), text.size());
    result.append(cache.reset.data(), cache.reset.size());
    return result;
}

namespace ansi {

inline std::string colorize(std::string_view text,
                                  const Color& fg_color,
                                  StyleFlag flags,
                                  const TerminalCapabilities& cap) {
    if (!cap.is_tty || text.empty()) {
        return std::string(text);
    }
    std::string result;
    result.reserve(text.size() + 64);
    auto f = fg(fg_color, cap);
    auto s = style(flags);
    if (!f.empty()) result.append(f.data(), f.size());
    if (!s.empty()) result.append(s.data(), s.size());
    result.append(text.data(), text.size());
    auto& r = reset();
    result.append(r.data(), r.size());
    return result;
}

inline std::string colorize(std::string_view text,
                                  const Color& fg_color,
                                  const Color& bg_color,
                                  StyleFlag flags,
                                  const TerminalCapabilities& cap) {
    if (!cap.is_tty || text.empty()) {
        return std::string(text);
    }
    std::string result;
    result.reserve(text.size() + 96);
    auto f = fg(fg_color, cap);
    auto b = bg(bg_color, cap);
    auto s = style(flags);
    if (!f.empty()) result.append(f.data(), f.size());
    if (!b.empty()) result.append(b.data(), b.size());
    if (!s.empty()) result.append(s.data(), s.size());
    result.append(text.data(), text.size());
    auto& r = reset();
    result.append(r.data(), r.size());
    return result;
}

}  // namespace ansi

}  // namespace ben_gear::cli
