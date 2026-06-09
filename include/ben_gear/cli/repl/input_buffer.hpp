#pragma once

#include "ben_gear/base/container/string.hpp"

#include <string_view>

namespace ben_gear::cli {

namespace container = base::container;

/// UTF-8 字节分类和显示宽度工具
namespace utf8 {

/// 是否为 ASCII 字节 (0x00-0x7F)
inline bool is_ascii(unsigned char c) { return c <= 0x7F; }

/// 是否为 UTF-8 续字节 (0x80-0xBF)
inline bool is_continuation(unsigned char c) { return (c & 0xC0) == 0x80; }

/// 是否为 UTF-8 多字节序列首字节 (0xC0-0xFF)
inline bool is_leading(unsigned char c) { return c >= 0xC0; }

/// 根据 UTF-8 首字节推断该字符总字节数
inline int sequence_length(unsigned char c) {
    if (c <= 0x7F) return 1;
    if (c <= 0xDF) return 2;
    if (c <= 0xEF) return 3;
    if (c <= 0xF7) return 4;
    return 1; // 非法首字节，按 1 字节处理
}

/// 计算 UTF-8 字符串的显示宽度（列数）
/// ASCII 字符占 1 列，CJK 字符占 2 列
/// 跳过续字节，只按首字节统计
inline size_t display_width(std::string_view str) {
    size_t width = 0;
    size_t i = 0;
    while (i < str.size()) {
        auto byte = static_cast<unsigned char>(str[i]);
        if (is_continuation(byte)) {
            // 孤立续字节，跳过
            ++i;
            continue;
        }
        int seq_len = sequence_length(byte);
        // CJK 字符范围：U+2E80 以上（首字节 >= 0xE0 的 3/4 字节序列）
        // 简化判断：>= 3 字节的 UTF-8 序列视为宽字符
        if (seq_len >= 3) {
            width += 2;
        } else {
            width += 1;
        }
        i += seq_len;
    }
    return width;
}

/// 计算字符串前 n 字节的显示宽度
inline size_t display_width(std::string_view str, size_t byte_count) {
    return display_width(str.substr(0, byte_count));
}

} // namespace utf8

/// 行内容 + 光标管理
///
/// 职责：维护一行可编辑文本和光标位置
/// 零终端 I/O 依赖，纯数据操作
/// UTF-8 防御：拒绝孤立的续字节，确保缓冲区内容始终为合法 UTF-8
class InputBuffer {
public:
    InputBuffer() = default;

    std::string_view content() const { return {buf_.data(), buf_.size()}; }
    size_t cursor() const { return pos_; }
    bool empty() const { return buf_.empty(); }
    size_t size() const { return buf_.size(); }

    /// 当前内容的显示宽度（列数），考虑 CJK 宽字符（缓存优化）
    size_t display_width() const {
        if (!display_width_cache_valid_) {
            display_width_cache_ = utf8::display_width(content());
            display_width_cache_valid_ = true;
        }
        return display_width_cache_;
    }

    /// 光标位置对应的显示列数（缓存优化）
    size_t cursor_col() const {
        if (!cursor_col_cache_valid_ || cursor_col_pos_ != pos_) {
            cursor_col_cache_ = utf8::display_width(content(), pos_);
            cursor_col_cache_valid_ = true;
            cursor_col_pos_ = pos_;
        }
        return cursor_col_cache_;
    }

    // ---- 编辑操作 ----

    /// 插入单字节
    void insert(char c) {
        buf_.insert(pos_, c);
        ++pos_;
        invalidate_cache();
    }

    void insert(std::string_view str) {
        buf_.insert(pos_, str.data(), str.size());
        pos_ += str.size();
        invalidate_cache();
    }

    bool backspace() {
        if (pos_ == 0) return false;
        // 回退到前一个 UTF-8 字符的起始位置，删除完整字符
        size_t new_pos = prev_char_pos(pos_);
        buf_.erase(new_pos, pos_ - new_pos);
        pos_ = new_pos;
        invalidate_cache();
        return true;
    }

    bool delete_char() {
        if (pos_ >= buf_.size()) return false;
        // 向前删除完整的 UTF-8 字符
        size_t end = next_char_pos(pos_);
        buf_.erase(pos_, end - pos_);
        invalidate_cache();
        return true;
    }

    void kill_to_end() {
        buf_.erase(pos_);
        invalidate_cache();
    }

    void kill_to_start() {
        buf_.erase(0, pos_);
        pos_ = 0;
        invalidate_cache();
    }

    void backspace_word() {
        // 逐字符往前跳过空格，记录删除起点
        size_t original_pos = pos_;
        while (pos_ > 0) {
            size_t prev = prev_char_pos(pos_);
            if (buf_[prev] != ' ') break;
            pos_ = prev;
        }
        // 逐字符往前跳过非空格字符
        while (pos_ > 0) {
            size_t prev = prev_char_pos(pos_);
            if (buf_[prev] == ' ') break;
            pos_ = prev;
        }
        // 一次性删除从 pos_ 到 original_pos 的内容
        if (pos_ < original_pos) {
            buf_.erase(pos_, original_pos - pos_);
        }
        invalidate_cache();
    }

    // ---- 光标移动 ----

    bool cursor_left()  { 
        if (pos_ > 0) { 
            pos_ = prev_char_pos(pos_);
            cursor_col_cache_valid_ = false;
            return true; 
        } 
        return false; 
    }
    
    bool cursor_right() { 
        if (pos_ < buf_.size()) { 
            pos_ = next_char_pos(pos_);
            cursor_col_cache_valid_ = false;
            return true; 
        } 
        return false; 
    }
    
    void cursor_home()  { 
        pos_ = 0; 
        cursor_col_cache_valid_ = false;
    }
    
    void cursor_end()   { 
        pos_ = buf_.size(); 
        cursor_col_cache_valid_ = false;
    }

    // ---- 整体操作 ----

    void set(std::string_view str) {
        buf_.clear();
        buf_.append(str.data(), str.size());
        pos_ = buf_.size();
        invalidate_cache();
    }

    void clear() {
        buf_.clear();
        pos_ = 0;
        invalidate_cache();
    }

    // ---- UTF-8 字符导航辅助 ----

    /// 返回 pos 前一个 UTF-8 字符的起始位置
    size_t prev_char_pos(size_t pos) const {
        if (pos == 0) return 0;
        --pos;
        // 向前跳过续字节，直到找到首字节或 ASCII
        while (pos > 0 && utf8::is_continuation(static_cast<unsigned char>(buf_[pos]))) {
            --pos;
        }
        return pos;
    }

    /// 返回 pos 后一个 UTF-8 字符的起始位置
    size_t next_char_pos(size_t pos) const {
        if (pos >= buf_.size()) return buf_.size();
        // 跳过当前字符的所有字节
        auto byte = static_cast<unsigned char>(buf_[pos]);
        int seq_len = utf8::sequence_length(byte);
        size_t result = pos + seq_len;
        return result > buf_.size() ? buf_.size() : result;
    }

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
    void invalidate_cache() {
        display_width_cache_valid_ = false;
        cursor_col_cache_valid_ = false;
    }
};

}  // namespace ben_gear::cli
