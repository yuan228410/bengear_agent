#pragma once

#include "cli/render/theme.hpp"
#include "cli/render/terminal.hpp"
#include "cli/render/highlight.hpp"
#include "base/container/string.hpp"
#include "base/container/vector.hpp"

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
                     const SyntaxHighlighter& highlighter);

    container::String feed(std::string_view token);

    container::String flush();

    void reset();

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

    // ---- 标题层级追踪 ----
    mutable int heading_level_ = 0;  // 当前标题层级（H3+ 子内容需缩进）

    // ---- 连续空行折叠 ----
    bool prev_line_blank_ = false;  // 上一行是否为空行（最多保留1个空行）

    // 返回子内容缩进空格数（H3=2, H4=4, ...）
    int content_indent() const;

    // 直接往 output 写缩进空格，零分配
    static void append_indent(container::String& out, int spaces);

    // ==================== 代码块开始检测 ====================

    bool is_code_fence_start(const container::String& line) const;

    void enter_code_fence(const container::String& line);

    // ==================== 代码块字符处理 ====================

    void handle_code_fence(char c, container::String& output);

    void handle_code_fence_end(char c, container::String& output);

    // ==================== 代码块输出 ====================

    container::String flush_code_line();

    // ==================== 行重绘 ====================

    container::String make_redraw(const container::String& rendered) const;

    // ==================== 行级 Markdown 渲染 ====================

    container::String render_line(const container::String& line) const;

    // ==================== 水平分隔线 ====================

    bool is_horizontal_rule(const container::String& line) const;

    container::String render_horizontal_rule() const;

    // ==================== 标题 ====================

    container::String render_heading(const container::String& line, int level) const;

    // ==================== 引用块 ====================

    container::String render_blockquote(const container::String& line) const;

    // ==================== 无序列表 ====================

    bool is_unordered_list(const container::String& line) const;

    container::String render_unordered_list(const container::String& line) const;

    // ==================== 有序列表 ====================

    bool is_ordered_list(const container::String& line) const;

    container::String render_ordered_list(const container::String& line) const;

    // ==================== 任务列表 ====================

    bool is_task_list(const container::String& line) const;

    container::String render_task_list(const container::String& line) const;

    // ==================== 表格 ====================

    // ==================== 表格渲染（实时 + 二次对齐） ====================
    //
    // 渲染流程：
    // 1. 表格行实时输出原始字符 + make_redraw 基本样式（和普通行一样）
    // 2. 同时缓冲所有行到 table_rows_
    // 3. 表格结束时：光标上移 N 行，逐行 \033[2K\r 清除，输出对齐表格
    // 这样保证：用户第一时间看到内容（实时性），表格结束后完美对齐（美观性）

    /// 判断行是否为表格行（至少 2 个 |）
    bool is_table_row(const container::String& line) const;

    /// 统计表格行的列数（| 分隔的数量 - 1）
    static int count_table_cols(const container::String& line);

    /// 基本表格行渲染（实时阶段用，无对齐，仅添加边框样式）
    container::String render_table_row_basic(const container::String& line) const;

    /// 表格结束：光标上移，清除旧行，输出对齐表格
    void flush_aligned_table(container::String& output) const;

    /// 计算字符串终端显示宽度
    /// 支持：CJK 双宽、Emoji (含 VS16/ZWJ)、ANSI 转义码跳过
    static size_t display_width(std::string_view text);

    container::Vector<container::String> parse_table_cells(const container::String& line) const;

    /// 判断单元格是否为分隔格式
    static bool is_table_separator(const container::String& cell);

    /// 对齐方式
    enum class Align : uint8_t { left, center, right };

    /// 解析分隔行中的对齐方式
    static Align parse_align(const container::String& sep_cell);

    /// 渲染表格边框线（顶/中）
    /// 全部使用 ASCII 字符，避免 CJK 终端中 box-drawing 字符宽度不确定导致对齐错位
    void render_table_border_line(const container::Vector<size_t>& col_widths,
                                  const container::String& border_color,
                                  const container::String& reset_code,
                                  container::String& result) const;

    /// 渲染底边框线
    /// 全部使用 ASCII 字符，与 render_table_border_line 保持一致
    void render_table_bottom_border(const container::Vector<size_t>& col_widths,
                                     const container::String& border_color,
                                     const container::String& reset_code,
                                     container::String& result) const;

    /// 渲染对齐表格
    /// @param clear_lines 二次渲染模式：每行前加 \033[2K\r 清除旧行
    container::String render_aligned_table(const container::Vector<container::String>& rows,
                                           bool clear_lines = false, int indent = 0) const;


    /// 从 string_view 渲染内联格式（避免临时 String 构造）
    container::String render_inline_raw(std::string_view text) const;

    /// 兼容 container::String 版本
    container::String render_inline(const container::String& text) const;

    // ==================== 辅助函数 ====================

    /// 判断是否为单词字符（字母/数字/下划线）
    static bool is_word_char(char c);

    /// 判断范围内是否包含空格
    static bool has_space(const char* data, size_t len);
};

}  // namespace ben_gear::cli
