#pragma once

#include "ben_gear/cli/theme.hpp"
#include "ben_gear/cli/terminal.hpp"
#include "ben_gear/cli/highlight.hpp"
#include "ben_gear/base/container/string.hpp"

#include <cstring>
#include <string_view>

namespace ben_gear::cli {

namespace container = base::container;

/// Markdown 流式渲染器（ANSI 重绘方案）
///
/// 核心策略：每个字符即时输出（保证实时性），遇到换行时
/// 清除当前行并重绘为带 Markdown 样式的内容。
///
/// 流程（以 "## 北京\n" 为例）：
/// 1. 逐字符输出原始文本 "## 北京" → 用户看到实时文字
/// 2. 遇到 \n：发送 \033[2K\r（清当前行+回车到行首）
/// 3. 输出渲染后内容 "▸▸ 北京"（粗体+颜色）→ 用户看到样式
/// 4. 输出 \n → 换行
///
/// 关键：光标始终在当前行末尾，不需要 cursor_up！
/// cursor_up 会导致覆盖上一行内容，这是之前的 bug。
class MarkdownRenderer {
public:
    MarkdownRenderer(const Theme& theme, const TerminalCapabilities& cap,
                     const SyntaxHighlighter& highlighter)
        : theme_(theme), cap_(cap), highlighter_(highlighter) {}

    container::String feed(std::string_view token) {
        if (token.empty()) return {};

        container::String output;
        output.reserve(token.size() * 2 + 64);

        for (size_t i = 0; i < token.size(); ++i) {
            char c = token[i];

            // ---- 代码块状态 ----
            if (state_ == State::code_fence) {
                handle_code_fence(c, output);
                continue;
            }
            if (state_ == State::code_fence_end) {
                handle_code_fence_end(c, output);
                continue;
            }

            // ---- 普通文本状态 ----
            if (c == '\n') {
                // 换行：清除当前行的原始文本，重绘为 Markdown 样式
                if (!current_line_.empty()) {
                    if (is_code_fence_start(current_line_)) {
                        auto redraw = make_redraw(render_line(current_line_));
                        output.append(redraw.data(), redraw.size());
                        enter_code_fence(current_line_);
                    } else {
                        auto redraw = make_redraw(render_line(current_line_));
                        output.append(redraw.data(), redraw.size());
                    }
                }
                output.push_back('\n');
                current_line_.clear();
                continue;
            }

            // 累积原始文本 + 即时输出原始字符（保证实时性）
            current_line_.push_back(c);
            output.push_back(c);
        }

        return output;
    }

    container::String flush() {
        container::String output;

        // 代码块中剩余内容
        if (state_ == State::code_fence && !code_line_.empty()) {
            output.append(flush_code_line());
            output.push_back('\n');
        }
        if (state_ == State::code_fence_end) {
            for (int i = 0; i < fence_count_; ++i) code_line_.push_back(fence_char_);
            output.append(flush_code_line());
            output.push_back('\n');
        }

        // 当前未换行的行：清原始文本 + 重绘
        if (!current_line_.empty()) {
            auto redraw = make_redraw(render_line(current_line_));
            output.append(redraw.data(), redraw.size());
            output.push_back('\n');
            current_line_.clear();
        }

        reset();
        return output;
    }

    void reset() {
        state_ = State::text;
        current_line_.clear();
        code_line_.clear();
        code_lang_.clear();
        fence_char_ = '\0';
        fence_count_ = 0;
        fence_len_ = 0;
    }

private:
    const Theme& theme_;
    const TerminalCapabilities& cap_;
    const SyntaxHighlighter& highlighter_;

    enum class State : uint8_t { text, code_fence, code_fence_end };

    State state_ = State::text;
    container::String current_line_;
    container::String code_line_;
    container::String code_lang_;
    char fence_char_ = '\0';
    int fence_count_ = 0;
    int fence_len_ = 0;

    // ==================== 代码块开始检测 ====================

    bool is_code_fence_start(const container::String& line) const {
        if (line.size() < 3) return false;
        char c = line[0];
        if (c != '`' && c != '~') return false;
        for (size_t i = 1; i < line.size() && i < 3; ++i) {
            if (line[i] != c) return false;
        }
        return true;
    }

    void enter_code_fence(const container::String& line) {
        fence_char_ = line[0];
        fence_len_ = 0;
        for (size_t i = 0; i < line.size() && line[i] == fence_char_; ++i) ++fence_len_;

        size_t lang_start = static_cast<size_t>(fence_len_);
        while (lang_start < line.size() && (line[lang_start] == ' ' || line[lang_start] == '\t')) ++lang_start;
        code_lang_ = line.substr(lang_start);
        code_line_.clear();
        state_ = State::code_fence;
    }

    // ==================== ANSI 重绘 ====================

    /// 清除当前行 + 重绘
    /// 关键：光标在当前行末尾，\033[2K\r 清整行并回到行首，然后写入渲染后内容
    container::String make_redraw(const container::String& rendered) const {
        if (rendered.empty()) {
            // 行内容被完全替换（如表格分隔行），只需清行
            container::String result;
            if (cap_.is_tty) {
                result.append("\033[2K\r", 5);
            }
            return result;
        }

        if (!cap_.is_tty) return rendered;

        container::String output;
        output.reserve(rendered.size() + 8);

        // 清整行 + 回车到行首（不需要 cursor_up！光标就在当前行）
        output.append("\033[2K\r", 5);

        // 渲染后内容
        output.append(rendered.data(), rendered.size());

        return output;
    }

    // ==================== 行类型判断 + 完整渲染 ====================

    container::String render_line(const container::String& line) const {
        if (line.empty()) return {};

        // 代码块开始
        if (is_code_fence_start(line)) {
            return render_code_fence_start(line);
        }

        // 标题 # ~ ######
        if (line.starts_with('#')) {
            return render_heading(line);
        }

        // 分隔线 --- / *** / ___
        if (is_hr(line)) {
            return render_hr();
        }

        // 引用 > text
        if (line.starts_with("> ") || line == ">") {
            return render_quote(line);
        }

        // 无序列表 - item / * item
        if ((line.starts_with("- ") || line.starts_with("* ")) && line.size() >= 2) {
            return render_unordered_list(line);
        }

        // 有序列表 1. item
        if (line.size() >= 3 && isdigit(static_cast<unsigned char>(line[0]))) {
            auto dot = line.find(". ");
            if (dot != container::String::npos && dot > 0 && dot < 4) {
                return render_ordered_list(line, dot);
            }
        }

        // 表格 | cell | cell |
        if (line.starts_with('|')) {
            if (is_table_separator(line)) return {};  // 分隔行不渲染
            return render_table_row(line);
        }

        // 其他：渲染行内元素
        return render_inline(line);
    }

    // ==================== 标题 ====================

    container::String render_heading(const container::String& line) const {
        int level = 0;
        while (level < static_cast<int>(line.size()) && level < 6 && line[static_cast<size_t>(level)] == '#') ++level;
        size_t content_start = static_cast<size_t>(level);
        while (content_start < line.size() && line[content_start] == ' ') ++content_start;

        auto content = std::string_view(line.data() + content_start, line.size() - content_start);
        auto rendered = render_inline(container::String(content));

        container::String result;
        result.reserve(rendered.size() + 32);

        auto bold_code = ansi::bold();
        auto color_code = ansi::fg(theme_.assistant_heading, cap_);
        auto reset_code = ansi::reset();

        if (!bold_code.empty()) result.append(bold_code.data(), bold_code.size());
        if (!color_code.empty()) result.append(color_code.data(), color_code.size());

        // 层级标识
        if (cap_.unicode) {
            for (int i = 0; i < level; ++i) result.append("\xe2\x96\xb8", 3);  // ▸
        } else {
            for (int i = 0; i < level; ++i) result.push_back('#');
        }
        result.push_back(' ');

        result.append(rendered.data(), rendered.size());

        if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());
        return result;
    }

    // ==================== 代码块开始行渲染 ====================

    container::String render_code_fence_start(const container::String& line) const {
        container::String result;
        auto bg_code = ansi::bg(theme_.assistant_code_bg, cap_);
        if (!bg_code.empty()) result.append(bg_code.data(), bg_code.size());

        size_t lang_start = 0;
        char fc = line[0];
        while (lang_start < line.size() && line[lang_start] == fc) ++lang_start;
        while (lang_start < line.size() && (line[lang_start] == ' ' || line[lang_start] == '\t')) ++lang_start;

        if (lang_start < line.size()) {
            auto lang_text = std::string_view(line.data() + lang_start, line.size() - lang_start);
            auto lang_color = ansi::fg(theme_.assistant_code_lang, cap_);
            auto bold_code = ansi::bold();
            if (!lang_color.empty()) result.append(lang_color.data(), lang_color.size());
            if (!bold_code.empty()) result.append(bold_code.data(), bold_code.size());
            result.append(lang_text.data(), lang_text.size());
            auto reset_code = ansi::reset();
            if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());
        }

        return result;
    }

    // ==================== 分隔线 ====================

    bool is_hr(const container::String& line) const {
        if (line.size() < 3) return false;
        char c = line[0];
        if (c != '-' && c != '*' && c != '_') return false;
        for (size_t i = 1; i < line.size(); ++i) {
            if (line[i] != c && line[i] != ' ' && line[i] != '\t') return false;
        }
        return true;
    }

    container::String render_hr() const {
        container::String result;
        auto dim_code = ansi::fg(theme_.system_info, cap_);
        auto reset_code = ansi::reset();
        if (!dim_code.empty()) result.append(dim_code.data(), dim_code.size());
        int width = cap_.width > 0 ? cap_.width : 80;
        for (int i = 0; i < width; ++i) {
            if (cap_.unicode) {
                result.append("\xe2\x94\x80", 3);  // ─
            } else {
                result.push_back('-');
            }
        }
        if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());
        return result;
    }

    // ==================== 引用 ====================

    container::String render_quote(const container::String& line) const {
        auto content = line.size() > 2
            ? std::string_view(line.data() + 2, line.size() - 2)
            : std::string_view();

        container::String result;
        auto dim_code = ansi::dim();
        auto reset_code = ansi::reset();
        if (!dim_code.empty()) result.append(dim_code.data(), dim_code.size());
        if (cap_.unicode) {
            result.append("\xe2\x94\x82 ", 4);  // │
        } else {
            result.append("| ", 2);
        }
        auto rendered = render_inline(container::String(content));
        result.append(rendered.data(), rendered.size());
        if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());
        return result;
    }

    // ==================== 列表 ====================

    container::String render_unordered_list(const container::String& line) const {
        auto content = std::string_view(line.data() + 2, line.size() - 2);
        container::String result;
        if (cap_.unicode) {
            result.append("  \xe2\x80\xa2 ", 6);  // •
        } else {
            result.append("  - ", 4);
        }
        auto rendered = render_inline(container::String(content));
        result.append(rendered.data(), rendered.size());
        return result;
    }

    container::String render_ordered_list(const container::String& line, size_t dot) const {
        auto content = std::string_view(line.data() + dot + 2, line.size() - dot - 2);
        container::String result;
        result.append("  ");
        result.append(line.data(), dot + 2);
        auto rendered = render_inline(container::String(content));
        result.append(rendered.data(), rendered.size());
        return result;
    }

    // ==================== 表格 ====================

    container::String render_table_row(const container::String& line) const {
        if (line.size() < 2) return {};

        auto content = std::string_view(line.data() + 1, line.size() - 2);

        container::String result;
        result.push_back(' ');

        size_t last = 0;
        for (size_t i = 0; i <= content.size(); ++i) {
            if (i == content.size() || content[i] == '|') {
                auto cell = std::string_view(content.data() + last, i - last);
                size_t cs = 0, ce = cell.size();
                while (cs < ce && (cell[cs] == ' ' || cell[cs] == '\t')) ++cs;
                while (ce > cs && (cell[ce-1] == ' ' || cell[ce-1] == '\t')) --ce;

                auto cell_text = container::String(cell.data() + cs, ce - cs);
                auto rendered = render_inline(cell_text);
                result.append(rendered.data(), rendered.size());

                if (i < content.size()) {
                    if (cap_.unicode) {
                        result.append(" \xe2\x94\x82 ", 5);  // │
                    } else {
                        result.append(" | ", 3);
                    }
                }
                last = i + 1;
            }
        }
        return result;
    }

    bool is_table_separator(const container::String& line) const {
        for (size_t i = 0; i < line.size(); ++i) {
            char c = line[i];
            if (c != '|' && c != '-' && c != ':' && c != ' ' && c != '\t') return false;
        }
        return true;
    }

    // ==================== 代码块行处理 ====================

    void handle_code_fence(char c, container::String& output) {
        if (c == fence_char_) {
            state_ = State::code_fence_end;
            fence_count_ = 1;
            return;
        }
        if (c == '\n') {
            output.append(flush_code_line());
            output.push_back('\n');
            code_line_.clear();
            return;
        }
        code_line_.push_back(c);
    }

    void handle_code_fence_end(char c, container::String& output) {
        if (c == fence_char_) {
            ++fence_count_;
            return;
        }
        if (fence_count_ >= fence_len_) {
            auto reset_code = ansi::reset();
            if (!reset_code.empty()) output.append(reset_code.data(), reset_code.size());
            state_ = State::text;
            code_lang_.clear();
            if (c == '\n') {
                output.push_back('\n');
            } else {
                current_line_.push_back(c);
            }
            return;
        }
        for (int i = 0; i < fence_count_; ++i) code_line_.push_back(fence_char_);
        code_line_.push_back(c);
        state_ = State::code_fence;
    }

    container::String flush_code_line() const {
        if (code_line_.empty()) {
            container::String result;
            auto bg_code = ansi::bg(theme_.assistant_code_bg, cap_);
            if (!bg_code.empty()) result.append(bg_code.data(), bg_code.size());
            result.push_back(' ');
            auto reset_code = ansi::reset();
            if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());
            return result;
        }

        if (!code_lang_.empty() && highlighter_.supports(std::string_view(code_lang_.data(), code_lang_.size()))) {
            return highlighter_.highlight(
                std::string_view(code_line_.data(), code_line_.size()),
                std::string_view(code_lang_.data(), code_lang_.size()));
        }

        container::String result;
        result.reserve(code_line_.size() + 16);
        auto bg_code = ansi::bg(theme_.assistant_code_bg, cap_);
        auto fg_code = ansi::fg(theme_.assistant_code_text, cap_);
        if (!bg_code.empty()) result.append(bg_code.data(), bg_code.size());
        if (!fg_code.empty()) result.append(fg_code.data(), fg_code.size());
        result.append(code_line_.data(), code_line_.size());
        auto reset_code = ansi::reset();
        if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());
        return result;
    }

    // ==================== 行内元素 ====================

    container::String render_inline(const container::String& text) const {
        if (!cap_.color || text.empty()) return container::String(text);

        container::String result;
        result.reserve(text.size() + 64);

        size_t i = 0;
        while (i < text.size()) {
            // ~~strikethrough~~
            if (i + 1 < text.size() && text[i] == '~' && text[i+1] == '~') {
                size_t end = text.find("~~", i + 2);
                if (end != container::String::npos) {
                    result.append("\033[9m", 4);
                    result.append(text.data() + i + 2, end - i - 2);
                    auto reset_code = ansi::reset();
                    if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());
                    i = end + 2; continue;
                }
            }
            // **bold**
            if (i + 1 < text.size() && text[i] == '*' && text[i+1] == '*') {
                size_t end = text.find("**", i + 2);
                if (end != container::String::npos) {
                    auto bold_code = ansi::bold();
                    auto reset_code = ansi::reset();
                    if (!bold_code.empty()) result.append(bold_code.data(), bold_code.size());
                    result.append(text.data() + i + 2, end - i - 2);
                    if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());
                    i = end + 2; continue;
                }
            }
            // __bold__
            if (i + 1 < text.size() && text[i] == '_' && text[i+1] == '_') {
                size_t end = text.find("__", i + 2);
                if (end != container::String::npos) {
                    auto bold_code = ansi::bold();
                    auto reset_code = ansi::reset();
                    if (!bold_code.empty()) result.append(bold_code.data(), bold_code.size());
                    result.append(text.data() + i + 2, end - i - 2);
                    if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());
                    i = end + 2; continue;
                }
            }
            // *italic*
            if (text[i] == '*' && (i + 1 >= text.size() || text[i+1] != '*')) {
                size_t end = text.find('*', i + 1);
                if (end != container::String::npos && (end + 1 >= text.size() || text[end+1] != '*')) {
                    auto italic_code = ansi::italic();
                    auto reset_code = ansi::reset();
                    if (!italic_code.empty()) result.append(italic_code.data(), italic_code.size());
                    result.append(text.data() + i + 1, end - i - 1);
                    if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());
                    i = end + 1; continue;
                }
            }
            // _italic_
            if (text[i] == '_' && (i + 1 >= text.size() || text[i+1] != '_')) {
                size_t end = text.find('_', i + 1);
                if (end != container::String::npos && (end + 1 >= text.size() || text[end+1] != '_')) {
                    auto italic_code = ansi::italic();
                    auto reset_code = ansi::reset();
                    if (!italic_code.empty()) result.append(italic_code.data(), italic_code.size());
                    result.append(text.data() + i + 1, end - i - 1);
                    if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());
                    i = end + 1; continue;
                }
            }
            // `inline code`
            if (text[i] == '`') {
                size_t end = text.find('`', i + 1);
                if (end != container::String::npos) {
                    auto bg_code = ansi::bg(theme_.assistant_inline_code_bg, cap_);
                    auto fg_code = ansi::fg(theme_.assistant_inline_code_text, cap_);
                    auto reset_code = ansi::reset();
                    if (!bg_code.empty()) result.append(bg_code.data(), bg_code.size());
                    if (!fg_code.empty()) result.append(fg_code.data(), fg_code.size());
                    result.append(text.data() + i + 1, end - i - 1);
                    if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());
                    i = end + 1; continue;
                }
            }
            // [link](url)
            if (text[i] == '[') {
                size_t bracket_end = text.find(']', i + 1);
                if (bracket_end != container::String::npos &&
                    bracket_end + 1 < text.size() && text[bracket_end + 1] == '(') {
                    size_t paren_end = text.find(')', bracket_end + 2);
                    if (paren_end != container::String::npos) {
                        auto link_code = ansi::fg(theme_.assistant_link, cap_);
                        auto underline_code = ansi::style(StyleFlag::underline);
                        auto reset_code = ansi::reset();
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
};

}  // namespace ben_gear::cli
