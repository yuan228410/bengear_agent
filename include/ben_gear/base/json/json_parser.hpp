#pragma once

#include "ben_gear/base/json/json_dom.hpp"

#include <string_view>

namespace ben_gear::base::json {

// ==================== 解析器 ====================

/// 高性能 JSON 解析器
/// - 递归下降
/// - 零拷贝字符串
/// - SIMD 加速空白跳过（通过 json_simd.hpp）
class JsonParser {
public:
    /// 解析 JSON 文本
    static JsonValue parse(std::string_view input, container::String* error = nullptr);

private:
    explicit JsonParser(std::string_view input);

    JsonValue parse_value();
    JsonValue parse_object();
    JsonValue parse_array();
    JsonValue parse_string();
    JsonValue parse_number();
    JsonValue parse_literal(const char* expected, size_t len, JsonValue&& result);

    // 词法操作
    void skip_whitespace();
    char peek() const;
    char advance();
    bool at_end() const;
    bool match(char expected);

    // 错误处理
    void set_error(const char* msg);
    JsonValue error_value();

    const char* ptr_;
    const char* end_;
    const char* start_;  // 输入起始（用于零拷贝）
    container::String* error_;
    bool has_error_ = false;
};

} // namespace ben_gear::base::json
