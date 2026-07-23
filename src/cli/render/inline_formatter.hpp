#pragma once

#include "cli/render/theme.hpp"
#include "cli/render/terminal.hpp"

#include <string_view>
#include <string>
#include <cstring>

namespace ben_gear::cli {

/// 内联 Markdown 格式渲染器（header-only）
///
/// 职责：将 Markdown 内联标记渲染为 ANSI 终端样式：
///   **bold** / __bold__ → ANSI bold
///   *italic* / _italic_ → ANSI italic
///   `code`               → 彩色背景内联代码
///   [link](url)          → 彩色下划线链接
///   ~~strikethrough~~    → ANSI strikethrough + dim
///
/// 设计：零状态，纯函数，线程安全。Theme 决定颜色，TerminalCapabilities 决定
/// 是否输出 ANSI 或 Unicode 降级。
class InlineFormatter {
public:
    InlineFormatter(const Theme& theme, const TerminalCapabilities& cap,
                    const AnsiStyleCache& cache)
        : theme_(theme), cap_(cap), cache_(cache) {}

    /// 渲染 string_view 中的内联格式
    std::string render(std::string_view text) const {
        std::string result;
        result.reserve(text.size() + 32);

        size_t i = 0;
        while (i < text.size()) {
            // ~~strikethrough~~
            if (i + 1 < text.size() && text[i] == '~' && text[i+1] == '~') {
                size_t end = text.find("~~", i + 2);
                if (end != std::string_view::npos) {
                    auto strike = cache_.strikethrough;
                    auto dim_code = cache_.dim;
                    auto reset_code = cache_.reset;
                    if (!dim_code.empty()) result.append(dim_code.data(), dim_code.size());
                    if (!strike.empty()) result.append(strike.data(), strike.size());
                    result.append(text.data() + i + 2, end - i - 2);
                    if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());
                    i = end + 2; continue;
                }
            }
            // **bold**
            if (i + 1 < text.size() && text[i] == '*' && text[i+1] == '*') {
                size_t end = text.find("**", i + 2);
                if (end != std::string_view::npos) {
                    auto bold_code = cache_.bold;
                    auto reset_code = cache_.reset;
                    if (!bold_code.empty()) result.append(bold_code.data(), bold_code.size());
                    result.append(text.data() + i + 2, end - i - 2);
                    if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());
                    i = end + 2; continue;
                }
            }
            // __bold__
            if (i + 1 < text.size() && text[i] == '_' && text[i+1] == '_') {
                size_t end = text.find("__", i + 2);
                if (end != std::string_view::npos) {
                    auto bold_code = cache_.bold;
                    auto reset_code = cache_.reset;
                    if (!bold_code.empty()) result.append(bold_code.data(), bold_code.size());
                    result.append(text.data() + i + 2, end - i - 2);
                    if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());
                    i = end + 2; continue;
                }
            }
            // *italic*（左右必须是单词边界）
            if (text[i] == '*' && (i + 1 >= text.size() || text[i+1] != '*')) {
                if (i == 0 || !is_word_char(text[i-1])) {
                    size_t end = text.find('*', i + 1);
                    if (end != std::string_view::npos && (end + 1 >= text.size() || text[end+1] != '*')) {
                        if (end + 1 >= text.size() || !is_word_char(text[end+1])) {
                            auto italic_code = cache_.italic;
                            auto reset_code = cache_.reset;
                            if (!italic_code.empty()) result.append(italic_code.data(), italic_code.size());
                            result.append(text.data() + i + 1, end - i - 1);
                            if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());
                            i = end + 1; continue;
                        }
                    }
                }
            }
            // _italic_（左右必须是单词边界，且内容不含空格 → 避免变量名误判）
            if (text[i] == '_' && (i + 1 >= text.size() || text[i+1] != '_')) {
                if (i == 0 || !is_word_char(text[i-1])) {
                    size_t end = text.find('_', i + 1);
                    if (end != std::string_view::npos && (end + 1 >= text.size() || text[end+1] != '_')) {
                        if ((end + 1 >= text.size() || !is_word_char(text[end+1])) &&
                            !has_space(text.data() + i + 1, end - i - 1)) {
                            auto italic_code = cache_.italic;
                            auto reset_code = cache_.reset;
                            if (!italic_code.empty()) result.append(italic_code.data(), italic_code.size());
                            result.append(text.data() + i + 1, end - i - 1);
                            if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());
                            i = end + 1; continue;
                        }
                    }
                }
            }
            // `inline code`
            if (text[i] == '`') {
                size_t end = text.find('`', i + 1);
                if (end != std::string_view::npos) {
                    auto bg_code = cache_.assistant_inline_code_bg;
                    auto fg_code = cache_.assistant_inline_code_text;
                    auto reset_code = cache_.reset;
                    if (!bg_code.empty()) result.append(bg_code.data(), bg_code.size());
                    if (!fg_code.empty()) result.append(fg_code.data(), fg_code.size());
                    result.append(text.data() + i + 1, end - i - 1);
                    if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());
                    i = end + 1; continue;
                }
            }
            // [link](url) — 只显示链接文本
            if (text[i] == '[') {
                size_t bracket_end = text.find(']', i + 1);
                if (bracket_end != std::string_view::npos &&
                    bracket_end + 1 < text.size() && text[bracket_end + 1] == '(') {
                    size_t paren_end = text.find(')', bracket_end + 2);
                    if (paren_end != std::string_view::npos) {
                        auto link_code = cache_.assistant_link;
                        auto underline_code = cache_.underline;
                        auto reset_code = cache_.reset;
                        if (!link_code.empty()) result.append(link_code.data(), link_code.size());
                        if (!underline_code.empty()) result.append(underline_code.data(), underline_code.size());
                        result.append(text.data() + i + 1, bracket_end - i - 1);
                        if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());
                        i = paren_end + 1; continue;
                    }
                }
            }
            result.push_back(text[i]);
            ++i;
        }

        return result;
    }

    /// 兼容 std::string 的重载
    std::string render(const std::string& text) const {
        return render(std::string_view(text.data(), text.size()));
    }

    /// 计算字符串在终端中的显示宽度
    /// 支持：CJK 双宽、Emoji (含 VS16/ZWJ)、ANSI 转义码跳过
    /// 与 Python Rich 库 CELL_WIDTHS 数据对齐
    static size_t display_width(std::string_view text) {
        size_t width = 0;
        size_t i = 0;
        while (i < text.size()) {
            unsigned char c = static_cast<unsigned char>(text[i]);
            // ANSI CSI 序列不计入
            if (c == 0x1b && i + 1 < text.size() && text[i + 1] == '[') {
                i += 2;
                while (i < text.size()) {
                    unsigned char b = static_cast<unsigned char>(text[i]);
                    if (b >= 0x40 && b <= 0x7E) { ++i; break; }
                    ++i;
                }
                continue;
            }
            if (c <= 0x1f || c == 0x7f) { ++i; continue; }
            if (c <= 0x7e) { ++width; ++i; continue; }
            // UTF-8 多字节解码
            uint32_t cp = 0;
            int bytes = 0;
            if ((c & 0xE0) == 0xC0) { bytes = 2; cp = c & 0x1F; }
            else if ((c & 0xF0) == 0xE0) { bytes = 3; cp = c & 0x0F; }
            else if ((c & 0xF8) == 0xF0) { bytes = 4; cp = c & 0x07; }
            else { ++i; continue; }
            bool valid = true;
            for (int j = 1; j < bytes && (i + j) < text.size(); ++j) {
                unsigned char b = static_cast<unsigned char>(text[i + j]);
                if ((b & 0xC0) != 0x80) { valid = false; break; }
                cp = (cp << 6) | (b & 0x3F);
            }
            if (!valid) { ++i; continue; }
            i += bytes;
            // 跳过 Variation Selectors / ZWJ
            if (cp >= 0xFE00 && cp <= 0xFE0F) continue;
            if (cp == 0x200D) continue;
            // Regional Indicator 取 1 宽
            if (cp >= 0x1F1E6 && cp <= 0x1F1FF) { ++width; continue; }

            // Unicode 宽字符范围（与 Python Rich CELL_WIDTHS 对齐）
            bool is_wide = is_wide_char(cp);
            width += is_wide ? 2 : 1;
        }
        return width;
    }

private:
    const Theme& theme_;
    const TerminalCapabilities& cap_;
    const AnsiStyleCache& cache_;

    static bool is_word_char(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '_';
    }

    static bool has_space(const char* data, size_t len) {
        for (size_t i = 0; i < len; ++i) {
            if (data[i] == ' ') return true;
        }
        return false;
    }

    /// Unicode EAW=W/F 宽字符判断
    static bool is_wide_char(uint32_t cp) {
        return (cp >= 0x1100 && cp <= 0x115F) ||
               (cp >= 0x231A && cp <= 0x231B) ||
               (cp >= 0x2329 && cp <= 0x232A) ||
               (cp >= 0x23E9 && cp <= 0x23EC) ||
               (cp == 0x23F0) ||
               (cp == 0x23F3) ||
               (cp >= 0x25FD && cp <= 0x25FE) ||
               (cp >= 0x2614 && cp <= 0x2615) ||
               (cp >= 0x2648 && cp <= 0x2653) ||
               (cp == 0x267F) ||
               (cp == 0x2693) ||
               (cp == 0x26A1) ||
               (cp >= 0x26AA && cp <= 0x26AB) ||
               (cp >= 0x26BD && cp <= 0x26BE) ||
               (cp >= 0x26C4 && cp <= 0x26C5) ||
               (cp == 0x26CE) ||
               (cp == 0x26D4) ||
               (cp == 0x26EA) ||
               (cp >= 0x26F2 && cp <= 0x26F3) ||
               (cp == 0x26F5) ||
               (cp == 0x26FA) ||
               (cp == 0x26FD) ||
               (cp == 0x2705) ||
               (cp >= 0x270A && cp <= 0x270B) ||
               (cp == 0x2728) ||
               (cp == 0x274C) ||
               (cp == 0x274E) ||
               (cp >= 0x2753 && cp <= 0x2755) ||
               (cp == 0x2757) ||
               (cp >= 0x2795 && cp <= 0x2797) ||
               (cp == 0x27B0) ||
               (cp == 0x27BF) ||
               (cp >= 0x2B1B && cp <= 0x2B1C) ||
               (cp == 0x2B50) ||
               (cp == 0x2B55) ||
               (cp >= 0x2E80 && cp <= 0x2E99) ||
               (cp >= 0x2E9B && cp <= 0x2EF3) ||
               (cp >= 0x2F00 && cp <= 0x2FD5) ||
               (cp >= 0x2FF0 && cp <= 0x3029) ||
               (cp >= 0x3030 && cp <= 0x303E) ||
               (cp >= 0x3041 && cp <= 0x3096) ||
               (cp >= 0x309B && cp <= 0x30FF) ||
               (cp >= 0x3105 && cp <= 0x312F) ||
               (cp >= 0x3131 && cp <= 0x318E) ||
               (cp >= 0x3190 && cp <= 0x31E3) ||
               (cp >= 0x31EF && cp <= 0x321E) ||
               (cp >= 0x3220 && cp <= 0x3247) ||
               (cp >= 0x3250 && cp <= 0x4DBF) ||
               (cp >= 0x4E00 && cp <= 0xA48C) ||
               (cp >= 0xA490 && cp <= 0xA4C6) ||
               (cp >= 0xA960 && cp <= 0xA97C) ||
               (cp >= 0xAC00 && cp <= 0xD7A3) ||
               (cp >= 0xF900 && cp <= 0xFAFF) ||
               (cp >= 0xFE10 && cp <= 0xFE19) ||
               (cp >= 0xFE30 && cp <= 0xFE52) ||
               (cp >= 0xFE54 && cp <= 0xFE66) ||
               (cp >= 0xFE68 && cp <= 0xFE6B) ||
               (cp >= 0xFF01 && cp <= 0xFF60) ||
               (cp >= 0xFFE0 && cp <= 0xFFE6) ||
               (cp >= 0x16FE0 && cp <= 0x16FE3) ||
               (cp >= 0x17000 && cp <= 0x187F7) ||
               (cp >= 0x18800 && cp <= 0x18CD5) ||
               (cp >= 0x18D00 && cp <= 0x18D08) ||
               (cp >= 0x1AFF0 && cp <= 0x1AFF3) ||
               (cp >= 0x1AFF5 && cp <= 0x1AFFB) ||
               (cp >= 0x1AFFD && cp <= 0x1AFFE) ||
               (cp >= 0x1B000 && cp <= 0x1B122) ||
               (cp == 0x1B132) ||
               (cp >= 0x1B150 && cp <= 0x1B152) ||
               (cp == 0x1B155) ||
               (cp >= 0x1B164 && cp <= 0x1B167) ||
               (cp >= 0x1B170 && cp <= 0x1B2FB) ||
               (cp == 0x1F004) ||
               (cp == 0x1F0CF) ||
               (cp == 0x1F18E) ||
               (cp >= 0x1F191 && cp <= 0x1F19A) ||
               (cp >= 0x1F200 && cp <= 0x1F202) ||
               (cp >= 0x1F210 && cp <= 0x1F23B) ||
               (cp >= 0x1F240 && cp <= 0x1F248) ||
               (cp >= 0x1F250 && cp <= 0x1F251) ||
               (cp >= 0x1F260 && cp <= 0x1F265) ||
               (cp >= 0x1F300 && cp <= 0x1F320) ||
               (cp >= 0x1F32D && cp <= 0x1F335) ||
               (cp >= 0x1F337 && cp <= 0x1F37C) ||
               (cp >= 0x1F37E && cp <= 0x1F393) ||
               (cp >= 0x1F3A0 && cp <= 0x1F3CA) ||
               (cp >= 0x1F3CF && cp <= 0x1F3D3) ||
               (cp >= 0x1F3E0 && cp <= 0x1F3F0) ||
               (cp == 0x1F3F4) ||
               (cp >= 0x1F3F8 && cp <= 0x1F3FA) ||
               (cp >= 0x1F400 && cp <= 0x1F43E) ||
               (cp == 0x1F440) ||
               (cp >= 0x1F442 && cp <= 0x1F4FC) ||
               (cp >= 0x1F4FF && cp <= 0x1F53D) ||
               (cp >= 0x1F54B && cp <= 0x1F54E) ||
               (cp >= 0x1F550 && cp <= 0x1F567) ||
               (cp == 0x1F57A) ||
               (cp >= 0x1F595 && cp <= 0x1F596) ||
               (cp == 0x1F5A4) ||
               (cp >= 0x1F5FB && cp <= 0x1F64F) ||
               (cp >= 0x1F680 && cp <= 0x1F6C5) ||
               (cp == 0x1F6CC) ||
               (cp >= 0x1F6D0 && cp <= 0x1F6D2) ||
               (cp >= 0x1F6D5 && cp <= 0x1F6D7) ||
               (cp >= 0x1F6DC && cp <= 0x1F6DF) ||
               (cp >= 0x1F6EB && cp <= 0x1F6EC) ||
               (cp >= 0x1F6F4 && cp <= 0x1F6FC) ||
               (cp >= 0x1F7E0 && cp <= 0x1F7EB) ||
               (cp == 0x1F7F0) ||
               (cp >= 0x1F90C && cp <= 0x1F93A) ||
               (cp >= 0x1F93C && cp <= 0x1F945) ||
               (cp >= 0x1F947 && cp <= 0x1F9FF) ||
               (cp >= 0x1FA70 && cp <= 0x1FA7C) ||
               (cp >= 0x1FA80 && cp <= 0x1FA88) ||
               (cp >= 0x1FA90 && cp <= 0x1FABD) ||
               (cp >= 0x1FABF && cp <= 0x1FAC5) ||
               (cp >= 0x1FACE && cp <= 0x1FADB) ||
               (cp >= 0x1FAE0 && cp <= 0x1FAE8) ||
               (cp >= 0x1FAF0 && cp <= 0x1FAF8) ||
               (cp >= 0x20000 && cp <= 0x2FFFD) ||
               (cp >= 0x30000 && cp <= 0x3FFFD);
    }
};

}  // namespace ben_gear::cli
