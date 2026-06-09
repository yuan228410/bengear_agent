#pragma once

#include "ben_gear/cli/render/theme.hpp"
#include "ben_gear/cli/render/terminal.hpp"
#include "ben_gear/cli/render/highlight.hpp"
#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/vector.hpp"

#include <cstring>
#include <string_view>

namespace ben_gear::cli {

namespace container = base::container;

/// Markdown 流式渲染器（ANSI 重绘方案）
///
/// 核心策略：每个字符即时输出（保证实时性），遇到换行时
/// 清除当前行并重绘为带 Markdown 样式的内容。
///
/// 性能优化：
/// - 零正则、纯字符扫描，O(n) 单遍处理
/// - 最小化 container::String 临时分配
/// - 跨平台：所有 Unicode 字符都有 ASCII fallback
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

            // ---- 表格缓冲模式 ----
            // 实时输出原始字符保证实时性，缓冲行用于后续对齐渲染
            if (state_ == State::table) {
                if (c == '\n') {
                    if (!current_line_.empty() && is_table_row(current_line_)) {
                        // 继续缓冲：基本样式重绘 + 换行
                        auto redraw = make_redraw(render_table_row_basic(current_line_));
                        output.append(redraw.data(), redraw.size());
                        table_rows_.push_back(container::String(current_line_.data(), current_line_.size()));
                        current_line_.clear();
                        output.push_back('\n');
                        continue;
                    } else {
                        // 表格结束：光标上移，逐行清除并重绘对齐表格
                        flush_aligned_table(output);
                        state_ = State::text;
                        if (current_line_.empty()) {
                            output.push_back('\n');
                            continue;
                        }
                        // fall through：当前行交给普通文本处理
                    }
                } else {
                    current_line_.push_back(c);
                    output.push_back(c);
                    continue;
                }
            }

            // ---- 普通文本状态 ----
            if (c == '\n') {
                // 换行：清除当前行的原始文本，重绘为 Markdown 样式
                if (!current_line_.empty()) {
                    if (is_code_fence_start(current_line_)) {
                        auto redraw = make_redraw(render_line(current_line_));
                        output.append(redraw.data(), redraw.size());
                        enter_code_fence(current_line_);
                    } else if (is_table_row(current_line_)) {
                        // 首个表格行：基本样式重绘 + 进入缓冲模式
                        auto redraw = make_redraw(render_table_row_basic(current_line_));
                        output.append(redraw.data(), redraw.size());
                        table_rows_.push_back(container::String(current_line_.data(), current_line_.size()));
                        state_ = State::table;
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

        // 表格缓冲区
        if (state_ == State::table && !table_rows_.empty()) {
            flush_aligned_table(output);
            state_ = State::text;
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
        code_lang_shown_ = false;
        fence_char_ = '\0';
        fence_count_ = 0;
        fence_len_ = 0;
        table_rows_.clear();
    }

private:
    const Theme& theme_;
    const TerminalCapabilities& cap_;
    const SyntaxHighlighter& highlighter_;

    enum class State : uint8_t { text, code_fence, code_fence_end, table };

    State state_ = State::text;
    container::String current_line_;
    container::String code_line_;
    container::String code_lang_;
    bool code_lang_shown_ = false;  // 语言标签是否已显示（首行）
    char fence_char_ = '\0';
    int fence_count_ = 0;
    int fence_len_ = 0;

    // ---- 表格缓冲 ----
    container::Vector<container::String> table_rows_;

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
        code_lang_.clear();
        for (size_t i = lang_start; i < line.size(); ++i) {
            if (line[i] == ' ' || line[i] == '\t') break;
            code_lang_.push_back(line[i]);
        }

        state_ = State::code_fence;
        code_line_.clear();
        fence_count_ = 0;
        code_lang_shown_ = false;
    }

    // ==================== 代码块字符处理 ====================

    void handle_code_fence(char c, container::String& output) {
        if (c == fence_char_) {
            ++fence_count_;
            code_line_.push_back(c);
            return;
        }

        // 非围栏字符：如果之前累积了围栏字符，先判断是否闭合
        if (fence_count_ >= fence_len_) {
            // 结束围栏（围栏字符数量 >= 开始时的数量）
            state_ = State::code_fence_end;
            fence_count_ = 0;
            // 把当前字符交给 code_fence_end 处理
            handle_code_fence_end(c, output);
            return;
        }

        // 不是闭合：围栏字符属于代码内容
        fence_count_ = 0;
        code_line_.push_back(c);
    }

    void handle_code_fence_end(char c, container::String& output) {
        if (c == fence_char_) {
            ++fence_count_;
            code_line_.push_back(c);
            return;
        }

        // 非围栏字符：判断是否真正闭合
        if (fence_count_ >= fence_len_) {
            // 真正闭合：重绘代码块结束行
            output.append(flush_code_line());
            output.push_back('\n');
            state_ = State::text;
            code_line_.clear();
            fence_count_ = 0;
            // 当前字符作为普通文本处理
            if (c == '\n') {
                // 换行直接跳过（已处理）
            } else {
                current_line_.push_back(c);
            }
            return;
        }

        // 不是闭合：围栏字符是代码内容，继续代码块模式
        fence_count_ = 0;
        state_ = State::code_fence;
        code_line_.push_back(c);
    }

    // ==================== 代码块输出 ====================

    container::String flush_code_line() {
        container::String output;

        // 代码块背景
        auto bg_code = ansi::bg(theme_.assistant_code_bg, cap_);
        auto fg_code = ansi::fg(theme_.assistant_code_text, cap_);
        auto reset_code = ansi::reset();

        // 左侧装饰边框 " │ "
        if (!bg_code.empty()) output.append(bg_code.data(), bg_code.size());
        auto border_color = ansi::fg(theme_.assistant_code_lang, cap_);
        if (!border_color.empty()) output.append(border_color.data(), border_color.size());
        if (cap_.unicode) {
            output.append(" \xe2\x94\x82 ", 5);  // │
        } else {
            output.append(" | ", 3);
        }
        if (!border_color.empty()) { auto r = ansi::reset(); output.append(r.data(), r.size()); }
        if (!bg_code.empty()) output.append(bg_code.data(), bg_code.size());

        // 语法高亮（仅首行显示语言标签，其他行高亮代码）
        if (state_ == State::code_fence_end && fence_count_ > 0) {
            // 结束围栏行：显示围栏字符
            if (!fg_code.empty()) output.append(fg_code.data(), fg_code.size());
            output.append(code_line_.data(), code_line_.size());
        } else if (!code_lang_shown_ && !code_lang_.empty()) {
            // 首行：显示语言标签
            code_lang_shown_ = true;
            auto lang_fg = ansi::fg(theme_.assistant_code_lang, cap_);
            auto bold_code = ansi::bold();
            if (!lang_fg.empty()) output.append(lang_fg.data(), lang_fg.size());
            if (!bold_code.empty()) output.append(bold_code.data(), bold_code.size());
            output.append(code_lang_.data(), code_lang_.size());
            if (!reset_code.empty()) output.append(reset_code.data(), reset_code.size());
            if (!bg_code.empty()) output.append(bg_code.data(), bg_code.size());
        } else {
            // 代码行：语法高亮
            std::string_view lang_sv(code_lang_.data(), code_lang_.size());
            if (highlighter_.supports(lang_sv)) {
                auto highlighted = highlighter_.highlight(
                    std::string_view(code_line_.data(), code_line_.size()), lang_sv);
                if (!bg_code.empty()) output.append(bg_code.data(), bg_code.size());
                output.append(highlighted.data(), highlighted.size());
            } else {
                if (!fg_code.empty()) output.append(fg_code.data(), fg_code.size());
                output.append(code_line_.data(), code_line_.size());
            }
        }

        // 行末空白填充 + reset（确保背景色覆盖整行）
        if (!reset_code.empty()) output.append(reset_code.data(), reset_code.size());
        return output;
    }

    // ==================== 行重绘 ====================

    container::String make_redraw(const container::String& rendered) const {
        container::String output;
        output.append("\033[2K\r", 5);  // 清当前行 + 回车到行首
        output.append(rendered.data(), rendered.size());
        return output;
    }

    // ==================== 行级 Markdown 渲染 ====================

    container::String render_line(const container::String& line) const {
        // 空行
        if (line.empty()) return line;

        // 水平分隔线（---, ***, ___）
        if (is_horizontal_rule(line)) {
            return render_horizontal_rule();
        }

        // 标题（# ~ ######）
        int heading_level = 0;
        if (line[0] == '#') {
            for (size_t i = 0; i < line.size() && i < 6; ++i) {
                if (line[i] == '#') ++heading_level;
                else break;
            }
            // # 后必须跟空格或行尾
            if (heading_level > 0 &&
                (heading_level >= static_cast<int>(line.size()) || line[heading_level] == ' ')) {
                return render_heading(line, heading_level);
            }
        }

        // 引用块（> ）
        if (line[0] == '>') {
            return render_blockquote(line);
        }

        // 无序列表（- / * / + 后跟空格）
        if (is_unordered_list(line)) {
            return render_unordered_list(line);
        }

        // 有序列表（1. 后跟空格）
        if (is_ordered_list(line)) {
            return render_ordered_list(line);
        }

        // 任务列表（- [ ] / - [x]）
        if (is_task_list(line)) {
            return render_task_list(line);
        }

        // 表格行（含 | 且至少两个 |）
        if (is_table_row(line)) {
            container::Vector<container::String> rows;
            rows.push_back(line);
            return render_aligned_table(rows);
        }

        // 普通行：渲染内联格式
        return render_inline(line);
    }

    // ==================== 水平分隔线 ====================

    bool is_horizontal_rule(const container::String& line) const {
        if (line.size() < 3) return false;
        char c = line[0];
        if (c != '-' && c != '*' && c != '_') return false;
        int count = 0;
        for (size_t i = 0; i < line.size(); ++i) {
            if (line[i] == c) ++count;
            else if (line[i] != ' ' && line[i] != '\t') return false;
        }
        return count >= 3;
    }

    container::String render_horizontal_rule() const {
        container::String result;
        auto color = ansi::fg(theme_.assistant_hr, cap_);
        auto dim_code = ansi::dim();
        auto reset_code = ansi::reset();
        if (!dim_code.empty()) result.append(dim_code.data(), dim_code.size());
        if (!color.empty()) result.append(color.data(), color.size());
        // 根据终端宽度绘制分隔线（最多80字符，保守值）
        int w = cap_.width > 0 ? cap_.width : 80;
        if (w > 120) w = 120;
        if (cap_.unicode) {
            for (int i = 0; i < w; ++i) result.append("\xe2\x94\x80", 3);  // ─
        } else {
            for (int i = 0; i < w; ++i) result.push_back('-');
        }
        if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());
        return result;
    }

    // ==================== 标题 ====================

    container::String render_heading(const container::String& line, int level) const {
        container::String result;

        // 跳过 # 前缀和后续空格
        size_t start = static_cast<size_t>(level);
        while (start < line.size() && line[start] == ' ') ++start;

        std::string_view text(line.data() + start, line.size() - start);

        // 选择标题级别对应的颜色
        const Color& heading_color = (level == 1) ? theme_.assistant_heading_h1
                                   : (level == 2) ? theme_.assistant_heading_h2
                                   :                theme_.assistant_heading_h3;

        auto color_code = ansi::fg(heading_color, cap_);
        auto bold_code = ansi::bold();
        auto reset_code = ansi::reset();

        // H1/H2 底部装饰线
        if (level <= 2 && !text.empty()) {
            if (!color_code.empty()) result.append(color_code.data(), color_code.size());
            if (!bold_code.empty()) result.append(bold_code.data(), bold_code.size());

            // 标记符号：H1 用 ═, H2 用 ─
            if (level == 1) {
                if (cap_.unicode) {
                    result.append("\xe2\x95\x9e ", 4);  // ╞
                } else {
                    result.append(">> ", 3);
                }
            } else {
                if (cap_.unicode) {
                    result.append("\xe2\x94\x9c ", 4);  // ├
                } else {
                    result.append("> ", 2);
                }
            }

            result.append(text.data(), text.size());

            if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());

            // H1 底部双线装饰
            if (level == 1) {
                result.push_back('\n');
                result.append("\033[2K\r", 5);
                auto dim_code = ansi::dim();
                if (!dim_code.empty()) result.append(dim_code.data(), dim_code.size());
                if (!color_code.empty()) result.append(color_code.data(), color_code.size());
                if (cap_.unicode) {
                    result.append("\xe2\x95\x9e", 3);  // ╞
                    for (size_t i = 0; i < text.size() + 3; ++i) result.append("\xe2\x95\x90", 3);  // ═
                } else {
                    result.append(">>");
                    for (size_t i = 0; i < text.size() + 2; ++i) result.push_back('=');
                }
                if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());
            }
        } else if (!text.empty()) {
            // H3~H6：前缀 ### + 粗体
            if (!color_code.empty()) result.append(color_code.data(), color_code.size());
            if (!bold_code.empty()) result.append(bold_code.data(), bold_code.size());
            if (cap_.unicode) {
                result.append("\xe2\x97\x86 ", 4);  // ◆
            } else {
                result.append("# ", 2);
            }
            result.append(text.data(), text.size());
            if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());
        }

        return result;
    }

    // ==================== 引用块 ====================

    container::String render_blockquote(const container::String& line) const {
        container::String result;

        // 跳过 > 和后续空格
        size_t start = 1;
        int depth = 0;
        while (start < line.size() && line[start - 1] == '>') {
            ++depth;
            if (start < line.size() && line[start] == ' ') ++start;
            if (start < line.size() && line[start] == '>') ++start;
            else break;
        }
        // 重新解析：统计连续的 > 数量
        depth = 0;
        start = 0;
        while (start < line.size() && line[start] == '>') {
            ++depth;
            ++start;
            if (start < line.size() && line[start] == ' ') ++start;
        }

        // 渲染左侧竖线 + 内容
        auto border_color = ansi::fg(theme_.assistant_blockquote_border, cap_);
        auto text_color = ansi::fg(theme_.assistant_blockquote_text, cap_);
        auto reset_code = ansi::reset();

        for (int d = 0; d < depth; ++d) {
            if (!border_color.empty()) result.append(border_color.data(), border_color.size());
            if (cap_.unicode) {
                result.append(" \xe2\x94\x83 ", 5);  // ┃
            } else {
                result.append(" | ", 3);
            }
            if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());
        }

        if (!text_color.empty()) result.append(text_color.data(), text_color.size());
        auto dim_code = ansi::dim();
        if (!dim_code.empty()) result.append(dim_code.data(), dim_code.size());

        std::string_view content(line.data() + start, line.size() - start);
        // 引用块内也渲染内联格式
        auto inline_result = render_inline_raw(content);
        result.append(inline_result.data(), inline_result.size());

        if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());
        return result;
    }

    // ==================== 无序列表 ====================

    bool is_unordered_list(const container::String& line) const {
        if (line.empty()) return false;
        // 支持缩进：跳过前导空格
        size_t i = 0;
        while (i < line.size() && line[i] == ' ') ++i;
        if (i >= line.size()) return false;
        char marker = line[i];
        if (marker != '-' && marker != '*' && marker != '+') return false;
        if (i + 1 >= line.size()) return false;
        return line[i + 1] == ' ';
    }

    container::String render_unordered_list(const container::String& line) const {
        container::String result;

        // 计算缩进级别
        size_t indent = 0;
        size_t i = 0;
        while (i < line.size() && line[i] == ' ') { ++indent; ++i; }

        int level = static_cast<int>(indent / 2);

        // 列表标记颜色
        auto marker_color = ansi::fg(theme_.assistant_list_marker, cap_);
        auto reset_code = ansi::reset();

        // 缩进
        for (int l = 0; l < level; ++l) result.append("  ", 2);

        // 标记
        if (!marker_color.empty()) result.append(marker_color.data(), marker_color.size());
        if (cap_.unicode) {
            // 不同层级用不同符号
            const char* markers[] = {"\xe2\x80\xa2", "\xe2\x97\xa6", "\xe2\x97\x8b"};
            const char* marker = markers[level % 3];  // • ◦ ○
            result.append(marker, strlen(marker));
        } else {
            const char markers[] = {'*', '-', '+'};
            result.push_back(markers[level % 3]);
        }
        result.push_back(' ');
        if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());

        // 跳过标记和空格
        ++i;  // 跳过标记字符
        if (i < line.size() && line[i] == ' ') ++i;

        // 内容（渲染内联格式）
        std::string_view content(line.data() + i, line.size() - i);
        auto inline_result = render_inline_raw(content);
        result.append(inline_result.data(), inline_result.size());

        return result;
    }

    // ==================== 有序列表 ====================

    bool is_ordered_list(const container::String& line) const {
        if (line.empty()) return false;
        size_t i = 0;
        while (i < line.size() && line[i] == ' ') ++i;
        if (i >= line.size() || line[i] < '0' || line[i] > '9') return false;
        while (i < line.size() && line[i] >= '0' && line[i] <= '9') ++i;
        if (i + 1 >= line.size()) return false;
        return (line[i] == '.' || line[i] == ')') && line[i + 1] == ' ';
    }

    container::String render_ordered_list(const container::String& line) const {
        container::String result;

        // 缩进
        size_t i = 0;
        size_t indent = 0;
        while (i < line.size() && line[i] == ' ') { ++indent; ++i; }
        int level = static_cast<int>(indent / 3);

        for (int l = 0; l < level; ++l) result.append("   ", 3);

        // 标记颜色
        auto marker_color = ansi::fg(theme_.assistant_list_marker, cap_);
        auto bold_code = ansi::bold();
        auto reset_code = ansi::reset();

        if (!marker_color.empty()) result.append(marker_color.data(), marker_color.size());
        if (!bold_code.empty()) result.append(bold_code.data(), bold_code.size());

        // 数字 + 分隔符
        while (i < line.size() && line[i] >= '0' && line[i] <= '9') {
            result.push_back(line[i]);
            ++i;
        }
        result.push_back('.');
        result.push_back(' ');
        if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());

        ++i;  // 跳过分隔符
        if (i < line.size() && line[i] == ' ') ++i;

        // 内容
        std::string_view content(line.data() + i, line.size() - i);
        auto inline_result = render_inline_raw(content);
        result.append(inline_result.data(), inline_result.size());

        return result;
    }

    // ==================== 任务列表 ====================

    bool is_task_list(const container::String& line) const {
        if (line.empty()) return false;
        size_t i = 0;
        while (i < line.size() && line[i] == ' ') ++i;
        // - [ ] 或 - [x]
        if (i + 5 >= line.size()) return false;
        if (line[i] != '-' || line[i+1] != ' ' || line[i+2] != '[') return false;
        char checked = line[i+3];
        if (checked != ' ' && checked != 'x' && checked != 'X') return false;
        return line[i+4] == ']' && line[i+5] == ' ';
    }

    container::String render_task_list(const container::String& line) const {
        container::String result;

        size_t i = 0;
        while (i < line.size() && line[i] == ' ') ++i;

        // 跳过 "- "
        i += 2;

        bool checked = (line[i+1] == 'x' || line[i+1] == 'X');

        auto reset_code = ansi::reset();

        // 渲染复选框
        if (checked) {
            auto success_color = ansi::fg(theme_.tool_success_marker, cap_);
            if (!success_color.empty()) result.append(success_color.data(), success_color.size());
            if (cap_.unicode) {
                result.append("\xe2\x9c\x93", 3);  // ✓
            } else {
                result.append("[x]", 3);
            }
        } else {
            auto dim_code = ansi::dim();
            if (!dim_code.empty()) result.append(dim_code.data(), dim_code.size());
            if (cap_.unicode) {
                result.append("\xe2\x97\x8b", 3);  // ○
            } else {
                result.append("[ ]", 3);
            }
        }
        result.push_back(' ');
        if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());

        // 跳过 "[x] " 或 "[ ] "
        i += 4;  // [x] 或 [ ]
        if (i < line.size() && line[i] == ' ') ++i;

        // 内容（已完成则带删除线）
        std::string_view content(line.data() + i, line.size() - i);
        auto inline_result = render_inline_raw(content);
        if (checked) {
            auto strike = ansi::strikethrough();
            auto dim_code = ansi::dim();
            if (!dim_code.empty()) result.append(dim_code.data(), dim_code.size());
            if (!strike.empty()) result.append(strike.data(), strike.size());
        }
        result.append(inline_result.data(), inline_result.size());
        if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());

        return result;
    }

    // ==================== 表格 ====================

    // ==================== 表格渲染（实时 + 二次对齐） ====================
    //
    // 渲染流程：
    // 1. 表格行实时输出原始字符 + make_redraw 基本样式（和普通行一样）
    // 2. 同时缓冲所有行到 table_rows_
    // 3. 表格结束时：光标上移 N 行，逐行 \033[2K\r 清除，输出对齐表格
    // 这样保证：用户第一时间看到内容（实时性），表格结束后完美对齐（美观性）

    /// 判断行是否为表格行（至少 2 个 |）
    bool is_table_row(const container::String& line) const {
        if (line.empty()) return false;
        int pipe_count = 0;
        for (size_t i = 0; i < line.size(); ++i) {
            if (line[i] == '|') ++pipe_count;
        }
        return pipe_count >= 2;
    }

    /// 基本表格行渲染（实时阶段用，无对齐，仅添加边框样式）
    container::String render_table_row_basic(const container::String& line) const {
        container::String result;
        auto border_color = ansi::fg(theme_.assistant_table_border, cap_);
        auto reset_code = ansi::reset();

        size_t start = 0;
        if (start < line.size() && line[start] == '|') ++start;
        size_t end = line.size();
        while (end > start && (line[end-1] == ' ' || line[end-1] == '\r' || line[end-1] == '|')) --end;

        // 检查是否为分隔行
        bool is_sep = true;
        for (size_t s = start; s < end && is_sep; ++s) {
            if (line[s] != '-' && line[s] != ':' && line[s] != '|' && line[s] != ' ') is_sep = false;
        }
        if (is_sep) {
            if (!border_color.empty()) result.append(border_color.data(), border_color.size());
            result.append(cap_.unicode ? "\xe2\x94\x82" : "|", cap_.unicode ? 3 : 1);
            if (cap_.unicode) {
                for (size_t j = 0; j < 3; ++j) result.append("\xe2\x94\x80", 3);
            } else {
                result.append("---", 3);
            }
            if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());
            return result;
        }

        // 数据行
        if (!border_color.empty()) result.append(border_color.data(), border_color.size());
        result.append(cap_.unicode ? "\xe2\x94\x82" : "|", cap_.unicode ? 3 : 1);
        if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());

        size_t pos = start;
        while (pos < end) {
            size_t cell_start = pos;
            size_t cell_end = pos;
            while (cell_end < end && line[cell_end] != '|') ++cell_end;
            while (cell_start < cell_end && line[cell_start] == ' ') ++cell_start;
            size_t cell_content_end = cell_end;
            while (cell_content_end > cell_start && line[cell_content_end - 1] == ' ') --cell_content_end;

            result.push_back(' ');
            if (cell_start < cell_content_end) {
                std::string_view cell_text(line.data() + cell_start, cell_content_end - cell_start);
                auto inline_result = render_inline_raw(cell_text);
                result.append(inline_result.data(), inline_result.size());
            }
            result.push_back(' ');

            if (!border_color.empty()) result.append(border_color.data(), border_color.size());
            result.append(cap_.unicode ? "\xe2\x94\x82" : "|", cap_.unicode ? 3 : 1);
            if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());
            pos = cell_end + 1;
        }
        return result;
    }

    /// 表格结束：光标上移，清除旧行，输出对齐表格
    void flush_aligned_table(container::String& output) const {
        size_t row_count = table_rows_.size();
        if (row_count == 0) return;

        // 光标上移到表格起始行
        if (cap_.is_tty) {
            auto up = ansi::cursor_up(static_cast<int>(row_count));
            if (!up.empty()) output.append(up.data(), up.size());
        }

        // 渲染对齐表格（每行前加 \033[2K\r 清除旧行内容）
        auto table_output = render_aligned_table(table_rows_, true);
        output.append(table_output.data(), table_output.size());
    }

    /// 计算字符串终端显示宽度（CJK 双宽 + 跳过 ANSI 转义码）
    static size_t display_width(std::string_view text) {
        size_t width = 0;
        size_t i = 0;
        while (i < text.size()) {
            unsigned char c = static_cast<unsigned char>(text[i]);
            // ANSI 转义序列不计入
            if (c == 0x1b && i + 1 < text.size() && text[i + 1] == '[') {
                i += 2;
                while (i < text.size() && text[i] != 'm') ++i;
                if (i < text.size()) ++i;
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
            // CJK / 全角判断
            bool is_wide = (cp >= 0x1100 && cp <= 0x115F) ||
                           (cp >= 0x231A && cp <= 0x231B) ||
                           (cp >= 0x2329 && cp <= 0x232A) ||
                           (cp >= 0x23E9 && cp <= 0x23F3) ||
                           (cp >= 0x23F8 && cp <= 0x23FA) ||
                           (cp == 0x25EF) ||
                           (cp >= 0x2E80 && cp <= 0x2FFF) ||
                           (cp >= 0x3000 && cp <= 0x303E) ||
                           (cp >= 0x3041 && cp <= 0x3096) ||
                           (cp >= 0x3099 && cp <= 0x30FF) ||
                           (cp >= 0x3105 && cp <= 0x312F) ||
                           (cp >= 0x3131 && cp <= 0x318E) ||
                           (cp >= 0x3190 && cp <= 0x3247) ||
                           (cp >= 0x3250 && cp <= 0x4DBF) ||
                           (cp >= 0x4E00 && cp <= 0x9FFF) ||
                           (cp >= 0xA000 && cp <= 0xA4C6) ||
                           (cp >= 0xA960 && cp <= 0xA97C) ||
                           (cp >= 0xAC00 && cp <= 0xD7A3) ||
                           (cp >= 0xF900 && cp <= 0xFAFF) ||
                           (cp >= 0xFE10 && cp <= 0xFE19) ||
                           (cp >= 0xFE30 && cp <= 0xFE6B) ||
                           (cp >= 0xFF01 && cp <= 0xFF60) ||
                           (cp >= 0xFFE0 && cp <= 0xFFE6) ||
                           (cp >= 0x1F200 && cp <= 0x1F251) ||
                           (cp >= 0x1F300 && cp <= 0x1F64F) ||
                           (cp >= 0x1F680 && cp <= 0x1F6FF) ||
                           (cp >= 0x1F900 && cp <= 0x1F9FF) ||
                           (cp >= 0x20000 && cp <= 0x2FFFD) ||
                           (cp >= 0x30000 && cp <= 0x3FFFD);
            width += is_wide ? 2 : 1;
        }
        return width;
    }

    /// 解析一行为单元格
    container::Vector<container::String> parse_table_cells(const container::String& line) const {
        container::Vector<container::String> cells;
        size_t start = 0;
        if (start < line.size() && line[start] == '|') ++start;
        size_t end = line.size();
        while (end > start && (line[end-1] == ' ' || line[end-1] == '\r' || line[end-1] == '|')) --end;
        size_t pos = start;
        while (pos < end) {
            size_t cell_start = pos;
            size_t cell_end = pos;
            while (cell_end < end && line[cell_end] != '|') ++cell_end;
            while (cell_start < cell_end && line[cell_start] == ' ') ++cell_start;
            size_t cell_content_end = cell_end;
            while (cell_content_end > cell_start && line[cell_content_end - 1] == ' ') --cell_content_end;
            cells.push_back(container::String(line.data() + cell_start, cell_content_end - cell_start));
            pos = cell_end + 1;
        }
        return cells;
    }

    /// 判断单元格是否为分隔格式
    static bool is_table_separator(const container::String& cell) {
        if (cell.empty()) return false;
        for (size_t i = 0; i < cell.size(); ++i) {
            if (cell[i] != '-' && cell[i] != ':' && cell[i] != ' ') return false;
        }
        return true;
    }

    /// 对齐方式
    enum class Align : uint8_t { left, center, right };

    /// 解析分隔行中的对齐方式
    static Align parse_align(const container::String& sep_cell) {
        bool left_colon = !sep_cell.empty() && sep_cell[0] == ':';
        bool right_colon = !sep_cell.empty() && sep_cell[sep_cell.size() - 1] == ':';
        if (left_colon && right_colon) return Align::center;
        if (right_colon) return Align::right;
        return Align::left;
    }

    /// 渲染表格边框线（顶/中）
    void render_table_border_line(const container::Vector<size_t>& col_widths,
                                  bool is_top,
                                  const container::String& border_color,
                                  const container::String& reset_code,
                                  container::String& result) const {
        if (!border_color.empty()) result.append(border_color.data(), border_color.size());
        result.append(cap_.unicode ? (is_top ? "\xe2\x94\x8c" : "\xe2\x94\x9c") : "+", cap_.unicode ? 3 : 1);
        for (size_t c = 0; c < col_widths.size(); ++c) {
            size_t w = col_widths[c] + 2;
            if (cap_.unicode) {
                for (size_t j = 0; j < w; ++j) result.append("\xe2\x94\x80", 3);
            } else {
                for (size_t j = 0; j < w; ++j) result.push_back('-');
            }
            if (c + 1 < col_widths.size()) {
                if (cap_.unicode) {
                    result.append(is_top ? "\xe2\x94\xac" : "\xe2\x94\xbc", 3);
                } else {
                    result.push_back('+');
                }
            }
        }
        result.append(cap_.unicode ? (is_top ? "\xe2\x94\x90" : "\xe2\x94\xa4") : "+", cap_.unicode ? 3 : 1);
        if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());
    }

    /// 渲染底边框线
    void render_table_bottom_border(const container::Vector<size_t>& col_widths,
                                     const container::String& border_color,
                                     const container::String& reset_code,
                                     container::String& result) const {
        if (!border_color.empty()) result.append(border_color.data(), border_color.size());
        result.append(cap_.unicode ? "\xe2\x94\x94" : "+", cap_.unicode ? 3 : 1);
        for (size_t c = 0; c < col_widths.size(); ++c) {
            size_t w = col_widths[c] + 2;
            if (cap_.unicode) {
                for (size_t j = 0; j < w; ++j) result.append("\xe2\x94\x80", 3);
            } else {
                for (size_t j = 0; j < w; ++j) result.push_back('-');
            }
            if (c + 1 < col_widths.size()) {
                if (cap_.unicode) {
                    result.append("\xe2\x94\xb4", 3);
                } else {
                    result.push_back('+');
                }
            }
        }
        result.append(cap_.unicode ? "\xe2\x94\x98" : "+", cap_.unicode ? 3 : 1);
        if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());
    }

    /// 渲染对齐表格
    /// @param clear_lines 二次渲染模式：每行前加 \033[2K\r 清除旧行
    container::String render_aligned_table(const container::Vector<container::String>& rows,
                                           bool clear_lines = false) const {
        if (rows.empty()) return {};

        auto border_color = ansi::fg(theme_.assistant_table_border, cap_);
        auto header_color = ansi::fg(theme_.assistant_table_header, cap_);
        auto bold_code = ansi::bold();
        auto reset_code = ansi::reset();

        // 1. 解析所有行的单元格
        container::Vector<container::Vector<container::String>> all_cells;
        all_cells.reserve(rows.size());
        size_t max_cols = 0;
        int sep_row_index = -1;
        for (size_t r = 0; r < rows.size(); ++r) {
            auto cells = parse_table_cells(rows[r]);
            bool is_sep = !cells.empty();
            for (size_t c = 0; c < cells.size() && is_sep; ++c) {
                if (!is_table_separator(cells[c])) is_sep = false;
            }
            if (is_sep && sep_row_index < 0) sep_row_index = static_cast<int>(r);
            if (cells.size() > max_cols) max_cols = cells.size();
            all_cells.push_back(std::move(cells));
        }

        // 2. 解析对齐方式
        container::Vector<Align> aligns;
        aligns.reserve(max_cols);
        if (sep_row_index >= 0) {
            auto& sep_cells = all_cells[sep_row_index];
            for (size_t c = 0; c < max_cols; ++c)
                aligns.push_back(c < sep_cells.size() ? parse_align(sep_cells[c]) : Align::left);
        } else {
            for (size_t c = 0; c < max_cols; ++c) aligns.push_back(Align::left);
        }

        // 3. 计算每列最大显示宽度（仅非分隔行）
        container::Vector<size_t> col_widths;
        col_widths.reserve(max_cols);
        for (size_t c = 0; c < max_cols; ++c) col_widths.push_back(0);
        for (size_t r = 0; r < all_cells.size(); ++r) {
            if (static_cast<int>(r) == sep_row_index) continue;
            auto& cells = all_cells[r];
            for (size_t c = 0; c < cells.size() && c < max_cols; ++c) {
                size_t w = display_width(std::string_view(cells[c].data(), cells[c].size()));
                if (w > col_widths[c]) col_widths[c] = w;
            }
        }

        // 4. 渲染表格
        container::String result;

        // 顶边框
        if (clear_lines) result.append("\033[2K\r", 5);
        render_table_border_line(col_widths, true, border_color, reset_code, result);
        result.push_back('\n');

        for (size_t r = 0; r < all_cells.size(); ++r) {
            if (static_cast<int>(r) == sep_row_index) {
                // 分隔行 -> 中间边框
                if (clear_lines) result.append("\033[2K\r", 5);
                render_table_border_line(col_widths, false, border_color, reset_code, result);
                result.push_back('\n');
                continue;
            }

            auto& cells = all_cells[r];
            bool is_header = (sep_row_index > 0 && r < static_cast<size_t>(sep_row_index));

            if (clear_lines) result.append("\033[2K\r", 5);

            // 行首 |
            if (!border_color.empty()) result.append(border_color.data(), border_color.size());
            result.append(cap_.unicode ? "\xe2\x94\x82" : "|", cap_.unicode ? 3 : 1);
            if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());

            for (size_t c = 0; c < max_cols; ++c) {
                std::string_view cell_text;
                if (c < cells.size())
                    cell_text = std::string_view(cells[c].data(), cells[c].size());

                container::String rendered;
                if (!cell_text.empty()) rendered = render_inline_raw(cell_text);

                size_t text_w = display_width(cell_text);
                size_t padding = (col_widths[c] > text_w) ? col_widths[c] - text_w : 0;

                if (is_header) {
                    if (!header_color.empty()) result.append(header_color.data(), header_color.size());
                    if (!bold_code.empty()) result.append(bold_code.data(), bold_code.size());
                }

                result.push_back(' ');
                Align align = (c < aligns.size()) ? aligns[c] : Align::left;
                switch (align) {
                    case Align::left:
                        if (!rendered.empty()) result.append(rendered.data(), rendered.size());
                        for (size_t p = 0; p < padding; ++p) result.push_back(' ');
                        break;
                    case Align::right:
                        for (size_t p = 0; p < padding; ++p) result.push_back(' ');
                        if (!rendered.empty()) result.append(rendered.data(), rendered.size());
                        break;
                    case Align::center: {
                        size_t left_pad = padding / 2;
                        size_t right_pad = padding - left_pad;
                        for (size_t p = 0; p < left_pad; ++p) result.push_back(' ');
                        if (!rendered.empty()) result.append(rendered.data(), rendered.size());
                        for (size_t p = 0; p < right_pad; ++p) result.push_back(' ');
                        break;
                    }
                }
                result.push_back(' ');
                if (is_header && !reset_code.empty()) result.append(reset_code.data(), reset_code.size());

                // 列分隔 |
                if (!border_color.empty()) result.append(border_color.data(), border_color.size());
                result.append(cap_.unicode ? "\xe2\x94\x82" : "|", cap_.unicode ? 3 : 1);
                if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());
            }
            result.push_back('\n');
        }

        // 底边框
        if (clear_lines) result.append("\033[2K\r", 5);
        render_table_bottom_border(col_widths, border_color, reset_code, result);

        return result;
    }


    /// 从 string_view 渲染内联格式（避免临时 String 构造）
    container::String render_inline_raw(std::string_view text) const {
        container::String result;
        result.reserve(text.size() + 32);

        size_t i = 0;
        while (i < text.size()) {
            // ~~strikethrough~~
            if (i + 1 < text.size() && text[i] == '~' && text[i+1] == '~') {
                size_t end = text.find("~~", i + 2);
                if (end != std::string_view::npos) {
                    auto strike = ansi::strikethrough();
                    auto dim_code = ansi::dim();
                    auto reset_code = ansi::reset();
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
                if (end != std::string_view::npos) {
                    auto bold_code = ansi::bold();
                    auto reset_code = ansi::reset();
                    if (!bold_code.empty()) result.append(bold_code.data(), bold_code.size());
                    result.append(text.data() + i + 2, end - i - 2);
                    if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());
                    i = end + 2; continue;
                }
            }
            // *italic*（左右必须是单词边界）
            if (text[i] == '*' && (i + 1 >= text.size() || text[i+1] != '*')) {
                // 左边界：* 前不能是字母数字
                if (i == 0 || !is_word_char(text[i-1])) {
                    size_t end = text.find('*', i + 1);
                    if (end != std::string_view::npos && (end + 1 >= text.size() || text[end+1] != '*')) {
                        // 右边界：* 后不能是字母数字
                        if (end + 1 >= text.size() || !is_word_char(text[end+1])) {
                            auto italic_code = ansi::italic();
                            auto reset_code = ansi::reset();
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
                        // 右边界：_ 后不能是字母数字，且中间无空格
                        if ((end + 1 >= text.size() || !is_word_char(text[end+1])) &&
                            !has_space(text.data() + i + 1, end - i - 1)) {
                            auto italic_code = ansi::italic();
                            auto reset_code = ansi::reset();
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
                if (bracket_end != std::string_view::npos &&
                    bracket_end + 1 < text.size() && text[bracket_end + 1] == '(') {
                    size_t paren_end = text.find(')', bracket_end + 2);
                    if (paren_end != std::string_view::npos) {
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

    /// 兼容 container::String 版本
    container::String render_inline(const container::String& text) const {
        return render_inline_raw(std::string_view(text.data(), text.size()));
    }

    // ==================== 辅助函数 ====================

    /// 判断是否为单词字符（字母/数字/下划线）
    static bool is_word_char(char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '_';
    }

    /// 判断范围内是否包含空格
    static bool has_space(const char* data, size_t len) {
        for (size_t i = 0; i < len; ++i) {
            if (data[i] == ' ') return true;
        }
        return false;
    }
};

}  // namespace ben_gear::cli
