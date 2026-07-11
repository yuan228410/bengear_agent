#include "base/json/json_serializer.hpp"

#include <cstdio>
#include <cstring>

namespace ben_gear::base::json {

// ==================== 辅助函数 ====================

// 解析从 p 开始的 UTF-8 序列，返回有效序列的字节数。
// 若起始字节为 ASCII 返回 1；若字节序列非法（孤立续字节、截断、超长、
// 超出 U+10FFFF、代理区、过短）返回 1（单字节按无效处理）。
static size_t utf8_seq_len(const unsigned char* p, size_t remaining) {
    unsigned char c = p[0];
    if (c < 0x80) return 1;  // ASCII

    unsigned len = 0;
    unsigned cp = 0;
    if ((c & 0xE0) == 0xC0) { len = 2; cp = c & 0x1F; }
    else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; }
    else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07; }
    else return 1;  // 非法前导字节

    if (remaining < len) return 1;  // 截断的多字节序列
    for (unsigned k = 1; k < len; ++k) {
        if ((p[k] & 0xC0) != 0x80) return 1;  // 续字节非法
        cp = (cp << 6) | (p[k] & 0x3F);
    }

    // 码点范围与过短（过长的）检查
    if (cp > 0x10FFFF) return 1;
    if (cp >= 0xD800 && cp <= 0xDFFF) return 1;  // UTF-16 代理区非法
    if (len == 2 && cp < 0x80) return 1;
    if (len == 3 && cp < 0x800) return 1;
    if (len == 4 && cp < 0x10000) return 1;
    return len;
}

size_t JsonSerializer::escaped_string_size(std::string_view str) {
    size_t size = 2;  // 引号
    const unsigned char* p = reinterpret_cast<const unsigned char*>(str.data());
    for (size_t i = 0; i < str.size(); ++i) {
        unsigned char c = p[i];
        switch (c) {
        case '"':  case '\\': size += 2; break;
        case '\b': case '\f': case '\n': case '\r': case '\t': size += 2; break;
        default:
            if (c < 0x20) {
                size += 6;  // \uXXXX
            } else if (c >= 0x80) {
                // 校验 UTF-8：合法序列原样保留，非法序列替换为 \uFFFD
                size_t seq = utf8_seq_len(p + i, str.size() - i);
                if (seq > 1) {
                    size += seq;
                    i += seq - 1;  // 跳过已计数的续字节
                } else {
                    size += 6;  // \uFFFD
                }
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
    const unsigned char* p = reinterpret_cast<const unsigned char*>(data);
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = p[i];
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
            } else if (c >= 0x80) {
                // 校验 UTF-8：合法序列原样写入，非法序列替换为 \uFFFD
                size_t seq = utf8_seq_len(p + i, len - i);
                if (seq > 1) {
                    for (size_t k = 0; k < seq; ++k) *ptr++ = static_cast<char>(p[i + k]);
                    i += seq - 1;  // 跳过已写入的续字节
                } else {
                    // \uFFFD（替换字符）
                    *ptr++ = '\\'; *ptr++ = 'u'; *ptr++ = 'F';
                    *ptr++ = 'F'; *ptr++ = 'F'; *ptr++ = 'D';
                }
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