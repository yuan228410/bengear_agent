#include "cli/render/markdown.hpp"

#include <cstring>

namespace ben_gear::cli {

MarkdownRenderer::MarkdownRenderer(const Theme& theme, const TerminalCapabilities& cap,
                     const SyntaxHighlighter& highlighter,
                     const AnsiStyleCache& cache)
    : theme_(theme), cap_(cap), highlighter_(highlighter), cache_(cache),
      inline_formatter_(theme, cap, cache) {}

std::string MarkdownRenderer::feed(std::string_view token) {

    if (token.empty()) return {};

    std::string output;
    output.reserve(token.size() * 2 + 64);

    for (size_t i = 0; i < token.size(); ++i) {
        char c = token[i];

        // ---- 代码块状态 ----
        if (state_ == State::code_fence) {
            handle_code_fence(c, output);
            continue;
        }
        if (state_ == State::code_fence_end) {
            handle_code_fence_end(c, output, 0);
            continue;
        }

        // ---- 表格缓冲模式 ----
        // 实时输出原始字符保证实时性，缓冲行用于后续对齐渲染
        if (state_ == State::table) {
            if (c == '\n') {
                if (!current_line_.empty() && is_table_row(current_line_)) {
                    // 检测列数变化：不同列数的表格应分开渲染
                    int cur_cols = count_table_cols(current_line_);
                    bool cols_changed = false;
                    if (!table_rows_.empty()) {
                        int first_cols = count_table_cols(table_rows_[0]);
                        if (cur_cols != first_cols) cols_changed = true;
                    }
                    if (cols_changed) {
                        flush_aligned_table(output);
                        table_rows_.clear();
                        auto redraw_new = make_redraw(render_table_row_basic(current_line_));
                        output.append(redraw_new.data(), redraw_new.size());
                        table_rows_.push_back(std::string(current_line_.data(), current_line_.size()));
                        current_line_.clear();
                        output.push_back('\n');
                        continue;
                    }
                    auto redraw = make_redraw(render_table_row_basic(current_line_));
                    output.append(redraw.data(), redraw.size());
                    table_rows_.push_back(std::string(current_line_.data(), current_line_.size()));
                    current_line_.clear();
                    output.push_back('\n');
                    continue;
                } else {
                    flush_aligned_table(output);
                    table_rows_.clear();
                    state_ = State::text;
                    if (current_line_.empty()) {
                        output.push_back('\n');
                        continue;
                    }
                }
            } else {
                current_line_.push_back(c);
                output.push_back(c);
                continue;
            }
        }

        // ---- 普通文本状态 ----
        if (c == '\n') {
            bool cur_line_blank = current_line_.empty();
            // 连续空行完全折叠：不输出任何空行
            if (cur_line_blank) {
                continue;
            }
            // 换行：清除当前行的原始文本，重绘为 Markdown 样式
            if (!current_line_.empty()) {
                if (is_code_fence_start(current_line_)) {
                    heading_level_ = 0;  // 代码块重置层级
                    auto redraw = make_redraw(render_line(current_line_));
                    output.append(redraw.data(), redraw.size());
                    enter_code_fence(current_line_);
                } else if (is_table_row(current_line_)) {
                    // 首个表格行：基本样式重绘 + 进入缓冲模式
                    auto redraw = make_redraw(render_table_row_basic(current_line_));
                    output.append(redraw.data(), redraw.size());
                    table_rows_.push_back(std::string(current_line_.data(), current_line_.size()));
                    state_ = State::table;
                } else {
                    // 标题行不加缩进，内容行加缩进
                    auto rendered = render_line(current_line_);
                    bool is_heading = (!current_line_.empty() && current_line_[0] == '#');
                    int indent = (!is_heading) ? content_indent() : 0;
                    output.append("\033[2K\r", 5);
                    if (indent > 0) append_indent(output, indent);
                    output.append(rendered.data(), rendered.size());
                }
            }
            output.push_back('\n');
            current_line_.clear();
            prev_line_blank_ = cur_line_blank;
            continue;
        }

        // 累积原始文本 + 即时输出原始字符（保证实时性）
        current_line_.push_back(c);
        output.push_back(c);
    }

    return output;
}

std::string MarkdownRenderer::flush() {
    std::string output;

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
        table_rows_.clear();
        state_ = State::text;
    }

    // 当前未换行的行：清原始文本 + 重绘
    if (!current_line_.empty()) {
        auto rendered = render_line(current_line_);
        bool is_heading = (!current_line_.empty() && current_line_[0] == '#');
        int indent = (!is_heading) ? content_indent() : 0;
        output.append("\033[2K\r", 5);
        if (indent > 0) append_indent(output, indent);
        output.append(rendered.data(), rendered.size());
        output.push_back('\n');
        current_line_.clear();
    }

    reset();
    return output;
}

void MarkdownRenderer::reset() {
    state_ = State::text;
    current_line_.clear();
    code_line_.clear();
    code_lang_.clear();
    code_lang_shown_ = false;
    fence_char_ = '\0';
    fence_count_ = 0;
    fence_len_ = 0;
    at_code_line_start_ = false;
    table_rows_.clear();
    heading_level_ = 0;
    prev_line_blank_ = false;
    highlighter_.reset();  // 清除跨行多行注释状态
}

// 直接往 output 写缩进空格，零分配
void MarkdownRenderer::append_indent(std::string& out, int spaces) {
    for (int i = 0; i < spaces; ++i) out.push_back(' ');
}

// ==================== 代码块开始检测 ====================

bool MarkdownRenderer::is_code_fence_start(const std::string& line) const {
    if (line.size() < 3) return false;
    char c = line[0];
    if (c != '`' && c != '~') return false;
    for (size_t i = 1; i < line.size() && i < 3; ++i) {
        if (line[i] != c) return false;
    }
    return true;
}

void MarkdownRenderer::enter_code_fence(const std::string& line) {
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
    at_code_line_start_ = true;  // 代码块第一行
}

// ==================== 代码块字符处理 ====================

void MarkdownRenderer::handle_code_fence(char c, std::string& output) {
    if (c == fence_char_) {
        ++fence_count_;
        code_line_.push_back(c);
        return;
    }

    // 非围栏字符：仅当围栏位于行首时才闭合（防止代码中的 ``` 误闭合）
    if (fence_count_ >= fence_len_ && at_code_line_start_) {
        state_ = State::code_fence_end;
        auto saved_count = fence_count_;
        fence_count_ = 0;
        handle_code_fence_end(c, output, saved_count);
        return;
    }

    // 换行：逐行刷新代码行（保证流式可见）
    if (c == '\n') {
        fence_count_ = 0;
        output.append(flush_code_line());
        output.push_back('\n');
        code_line_.clear();
        at_code_line_start_ = true;
        return;
    }

    // 普通代码字符
    fence_count_ = 0;
    at_code_line_start_ = false;
    code_line_.push_back(c);
}

void MarkdownRenderer::handle_code_fence_end(char c, std::string& output, int closing_count) {
    if (c == fence_char_) {
        ++fence_count_;
        code_line_.push_back(c);
        return;
    }

    // 使用调用方传入的闭合计数 + 当前累积的围栏字符数
    if (closing_count + fence_count_ >= fence_len_) {
        output.append(flush_code_line());
        output.push_back('\n');
        state_ = State::text;
        code_line_.clear();
        fence_count_ = 0;
        if (c == '\n') {
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

std::string MarkdownRenderer::flush_code_line() {
    std::string output;

    // 代码块背景
    auto bg_code = cache_.assistant_code_bg;
    auto fg_code = cache_.assistant_code_text;
    auto reset_code = cache_.reset;

    // 左侧装饰边框 " │ "
    if (!bg_code.empty()) output.append(bg_code.data(), bg_code.size());
    auto border_color = cache_.assistant_code_lang;
    if (!border_color.empty()) output.append(border_color.data(), border_color.size());
    if (cap_.unicode) {
        output.append(" \xe2\x94\x82 ", 5);  // │
    } else {
        output.append(" | ", 3);
    }
    if (!border_color.empty()) { auto r = cache_.reset; output.append(r.data(), r.size()); }
    if (!bg_code.empty()) output.append(bg_code.data(), bg_code.size());

    // 语法高亮（仅首行显示语言标签，其他行高亮代码）
    if (state_ == State::code_fence_end && fence_count_ > 0) {
        // 结束围栏行：显示围栏字符
        if (!fg_code.empty()) output.append(fg_code.data(), fg_code.size());
        output.append(code_line_.data(), code_line_.size());
    } else if (!code_lang_shown_ && !code_lang_.empty()) {
        // 首行：显示语言标签
        code_lang_shown_ = true;
        auto lang_fg = cache_.assistant_code_lang;
        auto bold_code = cache_.bold;
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

std::string MarkdownRenderer::make_redraw(const std::string& rendered) const {
    std::string output;
    output.append("\033[2K\r", 5);  // 清当前行 + 回车到行首
    output.append(rendered.data(), rendered.size());
    return output;
}

// ==================== 行级 Markdown 渲染 ====================

std::string MarkdownRenderer::render_line(const std::string& line) const {
    // 空行
    if (line.empty()) return line;

    // 水平分隔线（---, ***, ___）
    if (is_horizontal_rule(line)) {
        heading_level_ = 0;  // 分隔线重置层级
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
        std::vector<std::string> rows;
        rows.push_back(line);
        return render_aligned_table(rows, false, content_indent());
    }

    // 普通行：渲染内联格式
    return inline_formatter_.render(line);
}

// ==================== 水平分隔线 ====================

bool MarkdownRenderer::is_horizontal_rule(const std::string& line) const {
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

std::string MarkdownRenderer::render_horizontal_rule() const {
    std::string result;
    auto color = cache_.assistant_hr;
    auto dim_code = cache_.dim;
    auto reset_code = cache_.reset;
    if (!dim_code.empty()) result.append(dim_code.data(), dim_code.size());
    if (!color.empty()) result.append(color.data(), color.size());
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

std::string MarkdownRenderer::render_heading(const std::string& line, int level) const {
    heading_level_ = level;  // 记录当前标题层级

    std::string result;

    // 跳过 # 前缀和后续空格
    size_t start = static_cast<size_t>(level);
    while (start < line.size() && line[start] == ' ') ++start;

    std::string_view text(line.data() + start, line.size() - start);

    const Color& heading_color = (level == 1) ? theme_.assistant_heading_h1
                                : (level == 2) ? theme_.assistant_heading_h2
                                :                theme_.assistant_heading_h3;

    auto color_code = ansi::fg(heading_color, cap_);
    auto bold_code = cache_.bold;
    auto reset_code = cache_.reset;

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

std::string MarkdownRenderer::render_blockquote(const std::string& line) const {
    std::string result;

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

    auto border_color = cache_.assistant_blockquote_border;
    auto text_color = cache_.assistant_blockquote_text;
    auto reset_code = cache_.reset;

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
    auto dim_code = cache_.dim;
    if (!dim_code.empty()) result.append(dim_code.data(), dim_code.size());

    std::string_view content(line.data() + start, line.size() - start);
    auto inline_result = inline_formatter_.render(content);
    result.append(inline_result.data(), inline_result.size());

    if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());
    return result;
}

// ==================== 无序列表 ====================

bool MarkdownRenderer::is_unordered_list(const std::string& line) const {
    if (line.empty()) return false;
    size_t i = 0;
    while (i < line.size() && line[i] == ' ') ++i;
    if (i >= line.size()) return false;
    char marker = line[i];
    if (marker != '-' && marker != '*' && marker != '+') return false;
    if (i + 1 >= line.size()) return false;
    return line[i + 1] == ' ';
}

std::string MarkdownRenderer::render_unordered_list(const std::string& line) const {
    std::string result;

    size_t indent = 0;
    size_t i = 0;
    while (i < line.size() && line[i] == ' ') { ++indent; ++i; }

    int level = static_cast<int>(indent / 2);

    auto marker_color = cache_.assistant_list_marker;
    auto reset_code = cache_.reset;

    for (int l = 0; l < level; ++l) result.append("  ", 2);

    if (!marker_color.empty()) result.append(marker_color.data(), marker_color.size());
    if (cap_.unicode) {
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

    std::string_view content(line.data() + i, line.size() - i);
    auto inline_result = inline_formatter_.render(content);
    result.append(inline_result.data(), inline_result.size());

    return result;
}

// ==================== 有序列表 ====================

bool MarkdownRenderer::is_ordered_list(const std::string& line) const {
    if (line.empty()) return false;
    size_t i = 0;
    while (i < line.size() && line[i] == ' ') ++i;
    if (i >= line.size() || line[i] < '0' || line[i] > '9') return false;
    while (i < line.size() && line[i] >= '0' && line[i] <= '9') ++i;
    if (i + 1 >= line.size()) return false;
    return (line[i] == '.' || line[i] == ')') && line[i + 1] == ' ';
}

std::string MarkdownRenderer::render_ordered_list(const std::string& line) const {
    std::string result;

    size_t i = 0;
    size_t indent = 0;
    while (i < line.size() && line[i] == ' ') { ++indent; ++i; }
    int level = static_cast<int>(indent / 3);

    for (int l = 0; l < level; ++l) result.append("   ", 3);

    auto marker_color = cache_.assistant_list_marker;
    auto bold_code = cache_.bold;
    auto reset_code = cache_.reset;

    if (!marker_color.empty()) result.append(marker_color.data(), marker_color.size());
    if (!bold_code.empty()) result.append(bold_code.data(), bold_code.size());

    while (i < line.size() && line[i] >= '0' && line[i] <= '9') {
        result.push_back(line[i]);
        ++i;
    }
    result.push_back('.');
    result.push_back(' ');
    if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());

    ++i;  // 跳过分隔符
    if (i < line.size() && line[i] == ' ') ++i;

    std::string_view content(line.data() + i, line.size() - i);
    auto inline_result = inline_formatter_.render(content);
    result.append(inline_result.data(), inline_result.size());

    return result;
}

// ==================== 任务列表 ====================

bool MarkdownRenderer::is_task_list(const std::string& line) const {
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

std::string MarkdownRenderer::render_task_list(const std::string& line) const {
    std::string result;

    size_t i = 0;
    while (i < line.size() && line[i] == ' ') ++i;

    i += 2;

    bool checked = (line[i+1] == 'x' || line[i+1] == 'X');

    auto reset_code = cache_.reset;

    if (checked) {
        auto success_color = cache_.tool_success_marker;
        if (!success_color.empty()) result.append(success_color.data(), success_color.size());
        if (cap_.unicode) {
            result.append("\xe2\x9c\x93", 3);  // ✓
        } else {
            result.append("[x]", 3);
        }
    } else {
        auto dim_code = cache_.dim;
        if (!dim_code.empty()) result.append(dim_code.data(), dim_code.size());
        if (cap_.unicode) {
            result.append("\xe2\x97\x8b", 3);  // ○
        } else {
            result.append("[ ]", 3);
        }
    }
    result.push_back(' ');
    if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());

    i += 4;  // [x] 或 [ ]
    if (i < line.size() && line[i] == ' ') ++i;

    std::string_view content(line.data() + i, line.size() - i);
    auto inline_result = inline_formatter_.render(content);
    if (checked) {
        auto strike = cache_.strikethrough;
        auto dim_code = cache_.dim;
        if (!dim_code.empty()) result.append(dim_code.data(), dim_code.size());
        if (!strike.empty()) result.append(strike.data(), strike.size());
    }
    result.append(inline_result.data(), inline_result.size());
    if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());

    return result;
}

// ==================== 表格渲染（实时 + 二次对齐） ====================
//
// 渲染流程：
// 1. 表格行实时输出原始字符 + make_redraw 基本样式（和普通行一样）
// 2. 同时缓冲所有行到 table_rows_
// 3. 表格结束时：光标上移 N 行，逐行 \033[2K\r 清除，输出对齐表格
// 这样保证：用户第一时间看到内容（实时性），表格结束后完美对齐（美观性）

/// 判断行是否为表格行（至少 2 个 |）
bool MarkdownRenderer::is_table_row(const std::string& line) const {
    if (line.empty()) return false;
    int pipe_count = 0;
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '|') ++pipe_count;
    }
    return pipe_count >= 2;
}

/// 统计表格行的列数（| 分隔的数量 - 1）
int MarkdownRenderer::count_table_cols(const std::string& line) {
    if (line.empty()) return 0;
    int pipe_count = 0;
    for (size_t i = 0; i < line.size(); ++i) {
        if (line[i] == '|') ++pipe_count;
    }
    // 首尾都有 | 时 cols = pipe_count - 1，否则 cols = pipe_count - 1
    return pipe_count > 0 ? pipe_count - 1 : 0;
}

/// 基本表格行渲染（实时阶段用，无对齐，仅添加边框样式）
std::string MarkdownRenderer::render_table_row_basic(const std::string& line) const {
    std::string result;
    auto border_color = cache_.assistant_table_border;
    auto reset_code = cache_.reset;

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
        result.push_back('|');
        if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());
        size_t cell_pos = start;
        int cell_idx = 0;
        while (cell_pos < end) {

            size_t ce = cell_pos;
            while (ce < end && line[ce] != '|') ++ce;
            if (cell_idx > 0) {
                if (!border_color.empty()) result.append(border_color.data(), border_color.size());
                result.push_back('|');
                if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());
            }
            result.append("---");
            cell_pos = ce + 1;
            ++cell_idx;
        }
        if (!border_color.empty()) result.append(border_color.data(), border_color.size());
        result.push_back('|');
        if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());
        return result;
    }

    if (!border_color.empty()) result.append(border_color.data(), border_color.size());
    result.push_back('|');
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
            auto inline_result = inline_formatter_.render(cell_text);
            result.append(inline_result.data(), inline_result.size());
        }
        result.push_back(' ');

        if (!border_color.empty()) result.append(border_color.data(), border_color.size());
        result.push_back('|');
        if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());
        pos = cell_end + 1;
    }
    return result;
}

/// 表格结束：光标上移，清除旧行，输出对齐表格
void MarkdownRenderer::flush_aligned_table(std::string& output) const {
    size_t row_count = table_rows_.size();
    if (row_count == 0) return;

    int indent = content_indent();

    // 光标上移到表格起始行
    // 注意：对齐表格比 basic 多 2 行（顶+底边框），
    // 多出的 2 行会延伸到 basic 表格下方（通常是空行/换行），
    // 不影响上方内容
    if (cap_.is_tty) {
        auto up = ansi::cursor_up(static_cast<int>(row_count));
        if (!up.empty()) output.append(up.data(), up.size());
    }

    // 渲染对齐表格（每行前加 \033[2K\r 清除旧行内容）
    auto table_output = render_aligned_table(table_rows_, true, indent);
    output.append(table_output.data(), table_output.size());
}


std::vector<std::string> MarkdownRenderer::parse_table_cells(const std::string& line) const {
    std::vector<std::string> cells;
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
        cells.push_back(std::string(line.data() + cell_start, cell_content_end - cell_start));
        pos = cell_end + 1;
    }
    return cells;
}

/// 判断单元格是否为分隔格式
bool MarkdownRenderer::is_table_separator(const std::string& cell) {
    if (cell.empty()) return false;
    for (size_t i = 0; i < cell.size(); ++i) {
        if (cell[i] != '-' && cell[i] != ':' && cell[i] != ' ') return false;
    }
    return true;
}

/// 解析分隔行中的对齐方式
MarkdownRenderer::Align MarkdownRenderer::parse_align(const std::string& sep_cell) {
    bool left_colon = !sep_cell.empty() && sep_cell[0] == ':';
    bool right_colon = !sep_cell.empty() && sep_cell[sep_cell.size() - 1] == ':';
    if (left_colon && right_colon) return Align::center;
    if (right_colon) return Align::right;
    return Align::left;
}

/// 渲染表格边框线（顶/中）
/// 全部使用 ASCII 字符，避免 CJK 终端中 box-drawing 字符宽度不确定导致对齐错位
void MarkdownRenderer::render_table_border_line(const std::vector<size_t>& col_widths,
                                  const std::string& border_color,
                                  const std::string& reset_code,
                                  std::string& result) const {
    if (!border_color.empty()) result.append(border_color.data(), border_color.size());
    result.push_back('+');
    if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());
    for (size_t c = 0; c < col_widths.size(); ++c) {
        size_t w = col_widths[c] + 2;
        if (!border_color.empty()) result.append(border_color.data(), border_color.size());
        for (size_t j = 0; j < w; ++j) result.push_back('-');
        if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());
        if (c + 1 < col_widths.size()) {
            if (!border_color.empty()) result.append(border_color.data(), border_color.size());
            result.push_back('+');
            if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());
        }
    }
    if (!border_color.empty()) result.append(border_color.data(), border_color.size());
    result.push_back('+');
    if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());
}

/// 渲染底边框线
/// 全部使用 ASCII 字符，与 render_table_border_line 保持一致
void MarkdownRenderer::render_table_bottom_border(const std::vector<size_t>& col_widths,
                                     const std::string& border_color,
                                     const std::string& reset_code,
                                     std::string& result) const {
    if (!border_color.empty()) result.append(border_color.data(), border_color.size());
    result.push_back('+');
    if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());
    for (size_t c = 0; c < col_widths.size(); ++c) {
        size_t w = col_widths[c] + 2;
        if (!border_color.empty()) result.append(border_color.data(), border_color.size());
        for (size_t j = 0; j < w; ++j) result.push_back('-');
        if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());
        if (c + 1 < col_widths.size()) {
            if (!border_color.empty()) result.append(border_color.data(), border_color.size());
            result.push_back('+');
            if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());
        }
    }
    if (!border_color.empty()) result.append(border_color.data(), border_color.size());
    result.push_back('+');
    if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());
}

/// 渲染对齐表格
/// @param clear_lines 二次渲染模式：每行前加 \033[2K\r 清除旧行
std::string MarkdownRenderer::render_aligned_table(const std::vector<std::string>& rows,
                                           bool clear_lines, int indent) const {
    if (rows.empty()) return {};

    auto border_color = cache_.assistant_table_border;
    auto header_color = cache_.assistant_table_header;
    auto bold_code = cache_.bold;
    auto reset_code = cache_.reset;

    // 1. 解析所有行的单元格
    std::vector<std::vector<std::string>> all_cells;
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
    std::vector<Align> aligns;
    aligns.reserve(max_cols);
    if (sep_row_index >= 0) {
        auto& sep_cells = all_cells[sep_row_index];
        for (size_t c = 0; c < max_cols; ++c)
            aligns.push_back(c < sep_cells.size() ? parse_align(sep_cells[c]) : Align::left);
    } else {
        for (size_t c = 0; c < max_cols; ++c) aligns.push_back(Align::left);
    }

    // 3. 计算每列最大显示宽度（仅非分隔行）
    std::vector<size_t> col_widths;
    col_widths.reserve(max_cols);
    for (size_t c = 0; c < max_cols; ++c) col_widths.push_back(0);
    for (size_t r = 0; r < all_cells.size(); ++r) {
        if (static_cast<int>(r) == sep_row_index) continue;
        auto& cells = all_cells[r];
        for (size_t c = 0; c < cells.size() && c < max_cols; ++c) {
            std::string rendered_cell;
            if (!cells[c].empty()) rendered_cell = inline_formatter_.render(std::string_view(cells[c].data(), cells[c].size()));
            size_t w = display_width(std::string_view(rendered_cell.data(), rendered_cell.size()));
            if (w > col_widths[c]) col_widths[c] = w;
        }
    }

    // 4. 渲染表格
    std::string result;

    // 顶边框
    if (clear_lines) result.append("\033[2K\r", 5);
    for (int s = 0; s < indent; ++s) result.push_back(' ');
    render_table_border_line(col_widths, border_color, reset_code, result);
    result.push_back('\n');

    for (size_t r = 0; r < all_cells.size(); ++r) {
        if (static_cast<int>(r) == sep_row_index) {
            // 分隔行 -> 中间边框
            if (clear_lines) result.append("\033[2K\r", 5);
            for (int s = 0; s < indent; ++s) result.push_back(' ');
            render_table_border_line(col_widths, border_color, reset_code, result);
            result.push_back('\n');
            continue;
        }

        auto& cells = all_cells[r];
        bool is_header = (sep_row_index > 0 && r < static_cast<size_t>(sep_row_index));

        if (clear_lines) result.append("\033[2K\r", 5);
        for (int s = 0; s < indent; ++s) result.push_back(' ');

        // 行首 |
        if (!border_color.empty()) result.append(border_color.data(), border_color.size());
        result.push_back('|');
        if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());

        for (size_t c = 0; c < max_cols; ++c) {
            std::string_view cell_text;
            if (c < cells.size())
                cell_text = std::string_view(cells[c].data(), cells[c].size());

            std::string rendered;
            if (!cell_text.empty()) rendered = inline_formatter_.render(cell_text);

            size_t text_w = display_width(std::string_view(rendered.data(), rendered.size()));
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
            result.push_back('|');
            if (!reset_code.empty()) result.append(reset_code.data(), reset_code.size());
        }
        result.push_back('\n');
    }

    // 底边框
    if (clear_lines) result.append("\033[2K\r", 5);
    for (int s = 0; s < indent; ++s) result.push_back(' ');
    render_table_bottom_border(col_widths, border_color, reset_code, result);
    result.push_back('\n');

    return result;
}


int MarkdownRenderer::content_indent() const { return heading_level_ >= 3 ? (heading_level_ - 2) * 2 : 0; }

}  // namespace ben_gear::cli
