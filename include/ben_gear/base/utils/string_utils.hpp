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

} // namespace ben_gear::base::utils

// 向 ben_gear 顶层导出，保持调用方便
namespace ben_gear {
using base::utils::to_lower;
using base::utils::trim;
using base::utils::estimate_text_tokens;
} // namespace ben_gear
