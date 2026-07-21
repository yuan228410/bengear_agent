#pragma once

#include "cli/render/theme.hpp"
#include "cli/render/terminal.hpp"
#include "cli/render/highlight.hpp"
#include "cli/render/inline_formatter.hpp"
#include <vector>

#include <cstring>
#include <string_view>

namespace ben_gear::cli {


/// Markdown 流式渲染器（ANSI 重绘方案）
///
/// 核心策略：每个字符即时输出（保证实时性），遇到换行时
/// 清除当前行并重绘为带 Markdown 样式的内容。
///
/// 性能优化：
/// - 零正则、纯字符扫描，O(n) 单遍处理
/// - 最小化 std::string 临时分配
/// - 跨平台：所有 Unicode 字符都有 ASCII fallback
class MarkdownRenderer {
public:
    MarkdownRenderer(const Theme& theme, const TerminalCapabilities& cap,
                     const SyntaxHighlighter& highlighter);

    std::string feed(std::string_view token);

    std::string flush();

    void reset();

    /// 计算字符串终端显示宽度（委托给 InlineFormatter）
    static size_t display_width(std::string_view text) {
        return InlineFormatter::display_width(text);
    }

private:
    const Theme& theme_;
    const TerminalCapabilities& cap_;
    const SyntaxHighlighter& highlighter_;
    InlineFormatter inline_formatter_;

    enum class State : uint8_t { text, code_fence, code_fence_end, table };

    State state_ = State::text;
    std::string current_line_;
    std::string code_line_;
    std::string code_lang_;
    bool code_lang_shown_ = false;
    char fence_char_ = '\0';
    int fence_count_ = 0;
    int fence_len_ = 0;

    // ---- 表格缓冲 ----
    std::vector<std::string> table_rows_;

    // ---- 标题层级追踪 ----
    mutable int heading_level_ = 0;

    // ---- 连续空行折叠 ----
    bool prev_line_blank_ = false;

    // 返回子内容缩进空格数（H3=2, H4=4, ...）
    int content_indent() const;

    // 直接往 output 写缩进空格
    static void append_indent(std::string& out, int spaces);

    // ==================== 代码块开始检测 ====================

    bool is_code_fence_start(const std::string& line) const;
    void enter_code_fence(const std::string& line);

    // ==================== 代码块字符处理 ====================

    void handle_code_fence(char c, std::string& output);
    void handle_code_fence_end(char c, std::string& output, int closing_count);

    // ==================== 代码块输出 ====================

    std::string flush_code_line();

    // ==================== 行重绘 ====================

    std::string make_redraw(const std::string& rendered) const;

    // ==================== 行级 Markdown 渲染 ====================

    std::string render_line(const std::string& line) const;

    // ==================== 水平分隔线 ====================

    bool is_horizontal_rule(const std::string& line) const;
    std::string render_horizontal_rule() const;

    // ==================== 标题 ====================

    std::string render_heading(const std::string& line, int level) const;

    // ==================== 引用块 ====================

    std::string render_blockquote(const std::string& line) const;

    // ==================== 无序列表 ====================

    bool is_unordered_list(const std::string& line) const;
    std::string render_unordered_list(const std::string& line) const;

    // ==================== 有序列表 ====================

    bool is_ordered_list(const std::string& line) const;
    std::string render_ordered_list(const std::string& line) const;

    // ==================== 任务列表 ====================

    bool is_task_list(const std::string& line) const;
    std::string render_task_list(const std::string& line) const;

    // ==================== 表格 ====================

    bool is_table_row(const std::string& line) const;
    static int count_table_cols(const std::string& line);
    std::string render_table_row_basic(const std::string& line) const;
    void flush_aligned_table(std::string& output) const;
    std::vector<std::string> parse_table_cells(const std::string& line) const;
    static bool is_table_separator(const std::string& cell);

    enum class Align : uint8_t { left, center, right };
    static Align parse_align(const std::string& sep_cell);

    void render_table_border_line(const std::vector<size_t>& col_widths,
                                  const std::string& border_color,
                                  const std::string& reset_code,
                                  std::string& result) const;
    void render_table_bottom_border(const std::vector<size_t>& col_widths,
                                     const std::string& border_color,
                                     const std::string& reset_code,
                                     std::string& result) const;
    std::string render_aligned_table(const std::vector<std::string>& rows,
                                           bool clear_lines = false, int indent = 0) const;
};

}  // namespace ben_gear::cli
