#pragma once

#include "ben_gear/base/json/json_dom.hpp"

namespace ben_gear::base::json {

/// 高性能 JSON 序列化器
/// - 两遍序列化：预计算大小 + 单次写入
/// - 无缩进模式（compact）零开销
class JsonSerializer {
public:
    /// 序列化为 container::String
    static container::String serialize(const JsonValue& root, int indent = -1);

private:
    // 第一遍：计算输出大小
    static size_t compute_size(const JsonValue& val, int indent, int depth);

    // 第二遍：写入输出缓冲区
    static char* write(const JsonValue& val, char* ptr, int indent, int depth);

    // 辅助：计算字符串转义后大小
    static size_t escaped_string_size(std::string_view str);
    static size_t escaped_string_size(const container::String& str);

    // 辅助：写入转义字符串
    static char* write_escaped_string(const char* data, size_t len, char* ptr);
    static char* write_escaped_string(const container::String& str, char* ptr);

    // 辅助：整数转字符串
    static size_t int64_to_size(int64_t val);
    static size_t uint64_to_size(uint64_t val);
    static char* write_int64(int64_t val, char* ptr);
    static char* write_uint64(uint64_t val, char* ptr);
};

} // namespace ben_gear::base::json
