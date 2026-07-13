#pragma once

#include "base/container/string.hpp"

#include <string_view>

namespace ben_gear::cli {

namespace container = base::container;

/// UTF-8 字节分类和显示宽度工具
namespace utf8 {

/// 是否为 ASCII 字节 (0x00-0x7F)
bool is_ascii(unsigned char c);

/// 是否为 UTF-8 续字节 (0x80-0xBF)
bool is_continuation(unsigned char c);

/// 是否为 UTF-8 多字节序列首字节 (0xC0-0xFF)
bool is_leading(unsigned char c);

/// 根据 UTF-8 首字节推断该字符总字节数
int sequence_length(unsigned char c);

/// 计算 UTF-8 字符串的显示宽度（列数）
/// ASCII 字符占 1 列，CJK 字符占 2 列
/// 跳过续字节，只按首字节统计
size_t display_width(std::string_view str);

/// 计算字符串前 n 字节的显示宽度
size_t display_width(std::string_view str, size_t byte_count);

} // namespace utf8

/// 行内容 + 光标管理
///
/// 职责：维护一行可编辑文本和光标位置
/// 零终端 I/O 依赖，纯数据操作
/// UTF-8 防御：拒绝孤立的续字节，确保缓冲区内容始终为合法 UTF-8
class InputBuffer {
public:
    InputBuffer() = default;

    std::string_view content() const;
    size_t cursor() const;
    bool empty() const;
    size_t size() const;

    /// 当前内容的显示宽度（列数），考虑 CJK 宽字符（缓存优化）
    size_t display_width() const;

    /// 光标位置对应的显示列数（缓存优化）
    size_t cursor_col() const;

    // ---- 编辑操作 ----

    /// 插入单字节
    void insert(char c);

    void insert(std::string_view str);

    bool backspace();

    bool delete_char();

    void kill_to_end();

    void kill_to_start();

    void backspace_word();

    // ---- 光标移动 ----

    bool cursor_left();

    bool cursor_right();

    void cursor_home();

    void cursor_end();

    // ---- 整体操作 ----

    void set(std::string_view str);

    void clear();

    // ---- UTF-8 字符导航辅助 ----

    /// 返回 pos 前一个 UTF-8 字符的起始位置
    size_t prev_char_pos(size_t pos) const;

    /// 返回 pos 后一个 UTF-8 字符的起始位置
    size_t next_char_pos(size_t pos) const;

private:
    container::String buf_;
    size_t pos_ = 0;

    // ---- 显示宽度缓存 ----
    mutable size_t display_width_cache_ = 0;
    mutable bool display_width_cache_valid_ = false;

    // ---- 光标列缓存 ----
    mutable size_t cursor_col_cache_ = 0;
    mutable bool cursor_col_cache_valid_ = false;
    mutable size_t cursor_col_pos_ = 0;  // 缓存对应的光标位置

    /// 失效所有缓存
    void invalidate_cache();
};

}  // namespace ben_gear::cli
