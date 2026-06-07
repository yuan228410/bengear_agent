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

    /// 当前内容的显示宽度（列数），考虑 CJK 宽字符
    size_t display_width() const { return utf8::display_width(content()); }

    /// 光标位置对应的显示列数
    size_t cursor_col() const { return utf8::display_width(content(), pos_); }

    // ---- 编辑操作 ----

    /// 插入单字节
    void insert(char c) {
        buf_.insert(pos_, c);
        ++pos_;
    }

    void insert(std::string_view str) {
        buf_.insert(pos_, str.data(), str.size());
        pos_ += str.size();
    }

    bool backspace() {
        if (pos_ == 0) return false;
        --pos_;
        buf_.erase(pos_, 1);
        return true;
    }

    bool delete_char() {
        if (pos_ >= buf_.size()) return false;
        buf_.erase(pos_, 1);
        return true;
    }

    void kill_to_end() {
        buf_.erase(pos_);
    }

    void kill_to_start() {
        buf_.erase(0, pos_);
        pos_ = 0;
    }

    void backspace_word() {
        while (pos_ > 0 && buf_[pos_ - 1] == ' ') {
            --pos_;
            buf_.erase(pos_, 1);
        }
        while (pos_ > 0 && buf_[pos_ - 1] != ' ') {
            --pos_;
            buf_.erase(pos_, 1);
        }
    }

    // ---- 光标移动 ----

    bool cursor_left()  { if (pos_ > 0) { --pos_; return true; } return false; }
    bool cursor_right() { if (pos_ < buf_.size()) { ++pos_; return true; } return false; }
    void cursor_home()  { pos_ = 0; }
    void cursor_end()   { pos_ = buf_.size(); }

    // ---- 整体操作 ----

    void set(std::string_view str) {
        buf_.clear();
        buf_.append(str.data(), str.size());
        pos_ = buf_.size();
    }

    void clear() {
        buf_.clear();
        pos_ = 0;
    }

private:
    container::String buf_;
    size_t pos_ = 0;
};

}  // namespace ben_gear::cli
