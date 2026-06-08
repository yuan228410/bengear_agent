#include "ben_gear/base/json/json_serializer.hpp"

#include <cstdio>
#include <cstring>

namespace ben_gear::base::json {

// ==================== 辅助函数 ====================

size_t JsonSerializer::escaped_string_size(std::string_view str) {
    size_t size = 2;  // 引号
    for (size_t i = 0; i < str.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(str[i]);
        switch (c) {
        case '"':  case '\\': size += 2; break;
        case '\b': case '\f': case '\n': case '\r': case '\t': size += 2; break;
        default:
            if (c < 0x20) {
                size += 6;  // \uXXXX
            } else {
                size += 1;
            }
            break;
        }
    }
    return size;
}

size_t JsonSerializer::escaped_string_size(const container::String& str) {
    return escaped_string_size(std::string_view(str.data(), str.size()));
}

char* JsonSerializer::write_escaped_string(const char* data, size_t len, char* ptr) {
    *ptr++ = '"';
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = static_cast<unsigned char>(data[i]);
        switch (c) {
        case '"':  *ptr++ = '\\'; *ptr++ = '"';  break;
        case '\\': *ptr++ = '\\'; *ptr++ = '\\'; break;
        case '\b': *ptr++ = '\\'; *ptr++ = 'b';  break;
        case '\f': *ptr++ = '\\'; *ptr++ = 'f';  break;
        case '\n': *ptr++ = '\\'; *ptr++ = 'n';  break;
        case '\r': *ptr++ = '\\'; *ptr++ = 'r';  break;
        case '\t': *ptr++ = '\\'; *ptr++ = 't';  break;
        default:
            if (c < 0x20) {
                // \uXXXX
                *ptr++ = '\\'; *ptr++ = 'u'; *ptr++ = '0'; *ptr++ = '0';
                static const char hex[] = "0123456789ABCDEF";
                *ptr++ = hex[(c >> 4) & 0xF];
                *ptr++ = hex[c & 0xF];
            } else {
                *ptr++ = static_cast<char>(c);
            }
            break;
        }
    }
    *ptr++ = '"';
    return ptr;
}

char* JsonSerializer::write_escaped_string(const container::String& str, char* ptr) {
    return write_escaped_string(str.data(), str.size(), ptr);
}

size_t JsonSerializer::int64_to_size(int64_t val) {
    if (val == 0) return 1;
    size_t size = 0;
    if (val < 0) { size = 1; val = -val; }
    while (val > 0) { ++size; val /= 10; }
    return size;
}

size_t JsonSerializer::uint64_to_size(uint64_t val) {
    if (val == 0) return 1;
    size_t size = 0;
    while (val > 0) { ++size; val /= 10; }
    return size;
}

char* JsonSerializer::write_int64(int64_t val, char* ptr) {
    // 使用 snprintf 简单可靠
    int len = snprintf(ptr, 32, "%lld", static_cast<long long>(val));
    return ptr + len;
}

char* JsonSerializer::write_uint64(uint64_t val, char* ptr) {
    int len = snprintf(ptr, 32, "%llu", static_cast<unsigned long long>(val));
    return ptr + len;
}

// ==================== 两遍序列化 ====================

size_t JsonSerializer::compute_size(const JsonValue& val, int indent, int depth) {
    switch (val.type) {
    case JsonType::Null:
        return 4;  // "null"
    case JsonType::Bool:
        return val.bool_val ? 4 : 5;  // "true" / "false"
    case JsonType::Int:
        return int64_to_size(val.int_val);
    case JsonType::Uint:
        return uint64_to_size(val.uint_val);
    case JsonType::Double: {
        char buf[32];
        int len = snprintf(buf, sizeof(buf), "%.17g", val.double_val);
        return static_cast<size_t>(len);
    }
    case JsonType::String: {
        if (val.is_zero_copy()) {
            return escaped_string_size(std::string_view(val.sv_ptr, val.sv_len));
        }
        return escaped_string_size(*val.str_ptr);
    }
    case JsonType::Array: {
        size_t size = 2;  // "[]"
        if (val.arr_ptr->empty()) return size;
        bool compact = (indent < 0);
        size_t i = 0;
        for (const auto& item : *val.arr_ptr) {
            if (i > 0) size += compact ? 1 : (1 + 1);  // "," or ", "
            size += compute_size(item, indent, depth + 1);
            ++i;
        }
        if (!compact) {
            size += (val.arr_ptr->size()) * (1 + static_cast<size_t>(depth + 1) * static_cast<size_t>(indent));  // \n + indent
            size += 1 + static_cast<size_t>(depth) * static_cast<size_t>(indent);  // \n + indent for ]
        }
        return size;
    }
    case JsonType::Object: {
        size_t size = 2;  // "{}"
        if (val.obj_ptr->empty()) return size;
        bool compact = (indent < 0);
        size_t i = 0;
        for (const auto& entry : *val.obj_ptr) {
            if (i > 0) size += compact ? 1 : (1 + 1);  // ","
            size += escaped_string_size(std::string_view(entry.key.data(), entry.key.size()));
            size += compact ? 1 : 3;  // ":" or " : "
            size += compute_size(entry.value, indent, depth + 1);
            ++i;
        }
        if (!compact) {
            size += (val.obj_ptr->size()) * (1 + static_cast<size_t>(depth + 1) * static_cast<size_t>(indent));
            size += 1 + static_cast<size_t>(depth) * static_cast<size_t>(indent);
        }
        return size;
    }
    }
    return 0;
}

char* JsonSerializer::write(const JsonValue& val, char* ptr, int indent, int depth) {
    bool compact = (indent < 0);

    switch (val.type) {
    case JsonType::Null:
        std::memcpy(ptr, "null", 4);
        return ptr + 4;

    case JsonType::Bool:
        if (val.bool_val) {
            std::memcpy(ptr, "true", 4);
            return ptr + 4;
        } else {
            std::memcpy(ptr, "false", 5);
            return ptr + 5;
        }

    case JsonType::Int:
        return write_int64(val.int_val, ptr);

    case JsonType::Uint:
        return write_uint64(val.uint_val, ptr);

    case JsonType::Double: {
        int len = snprintf(ptr, 32, "%.17g", val.double_val);
        return ptr + len;
    }

    case JsonType::String: {
        if (val.is_zero_copy()) {
            return write_escaped_string(val.sv_ptr, val.sv_len, ptr);
        }
        return write_escaped_string(*val.str_ptr, ptr);
    }

    case JsonType::Array: {
        *ptr++ = '[';
        if (val.arr_ptr->empty()) {
            *ptr++ = ']';
            return ptr;
        }
        if (!compact) {
            *ptr++ = '\n';
            for (int d = 0; d < (depth + 1) * indent; ++d) *ptr++ = ' ';
        }
        size_t i = 0;
        for (const auto& item : *val.arr_ptr) {
            if (i > 0) {
                if (compact) {
                    *ptr++ = ',';
                } else {
                    *ptr++ = ',';
                    *ptr++ = '\n';
                    for (int d = 0; d < (depth + 1) * indent; ++d) *ptr++ = ' ';
                }
            }
            ptr = write(item, ptr, indent, depth + 1);
            ++i;
        }
        if (!compact) {
            *ptr++ = '\n';
            for (int d = 0; d < depth * indent; ++d) *ptr++ = ' ';
        }
        *ptr++ = ']';
        return ptr;
    }

    case JsonType::Object: {
        *ptr++ = '{';
        if (val.obj_ptr->empty()) {
            *ptr++ = '}';
            return ptr;
        }
        if (!compact) {
            *ptr++ = '\n';
            for (int d = 0; d < (depth + 1) * indent; ++d) *ptr++ = ' ';
        }
        size_t i = 0;
        for (const auto& entry : *val.obj_ptr) {
            if (i > 0) {
                if (compact) {
                    *ptr++ = ',';
                } else {
                    *ptr++ = ',';
                    *ptr++ = '\n';
                    for (int d = 0; d < (depth + 1) * indent; ++d) *ptr++ = ' ';
                }
            }
            ptr = write_escaped_string(std::string_view(entry.key.data(), entry.key.size()), ptr);
            if (compact) {
                *ptr++ = ':';
            } else {
                *ptr++ = ':'; *ptr++ = ' ';
            }
            ptr = write(entry.value, ptr, indent, depth + 1);
            ++i;
        }
        if (!compact) {
            *ptr++ = '\n';
            for (int d = 0; d < depth * indent; ++d) *ptr++ = ' ';
        }
        *ptr++ = '}';
        return ptr;
    }
    }
    return ptr;
}

container::String JsonSerializer::serialize(const JsonValue& root, int indent) {
    size_t size = compute_size(root, indent, 0);

    container::String result;
    result.reserve(size + 1);

    // 直接在 String 内部缓冲区写入
    // 需要一种方式直接写入内部缓冲区
    // 使用临时缓冲区方案
    char* buf = static_cast<char*>(::operator new(size + 1));
    char* end = write(root, buf, indent, 0);
    size_t actual_size = static_cast<size_t>(end - buf);

    result = container::String(buf, actual_size);
    ::operator delete(buf);

    return result;
}

} // namespace ben_gear::base::json
