#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

namespace ben_gear::base::utils {

inline std::string trim(std::string_view value) {
 auto begin = value.begin();
 auto end = value.end();
 while (begin != end && std::isspace(static_cast<unsigned char>(*begin))) {
  ++begin;
 }
 while (begin != end && std::isspace(static_cast<unsigned char>(*(end - 1)))) {
  --end;
 }
 return {begin, end};
}

inline std::string to_lower(std::string value) {
 std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
  return static_cast<char>(std::tolower(ch));
 });
 return value;
}

/// CJK 感知 token 估算
/// ASCII: ~4 字符 = 1 token
/// CJK/emoji: ~1 字符 = 2 token（GPT 分词器中 CJK 通常 1.5-2 token，取 2 保守高估）
/// 保守高估确保压缩触发偏早，避免接近上限才触发
inline int64_t estimate_text_tokens(std::string_view text) {
 int64_t cjk = 0;
 int64_t ascii = 0;
 for (size_t i = 0; i < text.size(); ) {
  unsigned char c = static_cast<unsigned char>(text[i]);
  if (c >= 0xF0)      { cjk++; i += 4; }
  else if (c >= 0xE0) { cjk++; i += 3; }
  else if (c >= 0xC0) { cjk++; i += 2; }
  else                { ascii++; i += 1; }
 }
 // CJK: 1 字符 ≈ 2 token（保守高估），ASCII: 4 字符 ≈ 1 token
 return cjk * 2 + std::max<int64_t>(1, ascii / 4);
}

/// 按字节上限截断，但保证不切断多字节 UTF-8 字符（切割点落在字符边界）。
/// 用于上下文裁剪/压缩：避免把中文等字符从中间劈开，产生非法 UTF-8。
inline std::string_view utf8_truncate(std::string_view s, size_t max_bytes) {
  if (s.size() <= max_bytes) return s;
  size_t i = 0;
  size_t boundary = 0;
  while (i < max_bytes) {
    unsigned char c = static_cast<unsigned char>(s[i]);
    size_t step = 1;
    if (c >= 0x80) {
      if ((c & 0xE0) == 0xC0) step = 2;
      else if ((c & 0xF0) == 0xE0) step = 3;
      else if ((c & 0xF8) == 0xF0) step = 4;
      else step = 1;  // 非法前导字节，按单字节跳过
    }
    if (i + step > max_bytes) break;  // 再走会越界，停在当前边界
    i += step;
    boundary = i;
  }
  return s.substr(0, boundary);
}

/// 与 utf8_truncate 对称：保留末尾最多 max_bytes 字节，但保证从字符边界开始。
inline std::string_view utf8_truncate_tail(std::string_view s, size_t max_bytes) {
  if (s.size() <= max_bytes) return s;
  size_t start = s.size() - max_bytes;
  // 前移到下一个合法字符起点（跳过多字节续字节 0x80-0xBF）
  while (start < s.size()) {
    unsigned char c = static_cast<unsigned char>(s[start]);
    if (c < 0x80 || (c & 0xC0) == 0xC0) break;  // ASCII 或前导字节
    ++start;
  }
  return s.substr(start);
}

} // namespace ben_gear::base::utils

// 向 ben_gear 顶层导出，保持调用方便
namespace ben_gear {
using base::utils::to_lower;
using base::utils::trim;
using base::utils::estimate_text_tokens;
using base::utils::utf8_truncate;
using base::utils::utf8_truncate_tail;
} // namespace ben_gear