#include "ben_gear/base/json/json_parser.hpp"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace ben_gear::base::json {

// ==================== 池化分配辅助（与 json_dom.cpp 一致）====================

inline container::String* pooled_new_string(const char* data, size_t len) {
    auto& pool = JsonPool::instance();
    void* ptr = pool.allocate_string();
    return new (ptr) container::String(data, len);
}

inline container::String* pooled_new_string(container::String&& other) {
    auto& pool = JsonPool::instance();
    void* ptr = pool.allocate_string();
    return new (ptr) container::String(std::move(other));
}

inline JsonObject* pooled_new_object() {
    auto& pool = JsonPool::instance();
    void* ptr = pool.allocate_object();
    return new (ptr) JsonObject();
}

inline JsonArray* pooled_new_array() {
    auto& pool = JsonPool::instance();
    void* ptr = pool.allocate_array();
    return new (ptr) JsonArray();
}

// ==================== JsonParser ====================

JsonValue JsonParser::parse(std::string_view input, container::String* error) {
    JsonParser parser(input);
    auto result = parser.parse_value();
    if (!parser.has_error_) {
        parser.skip_whitespace();
        if (!parser.at_end()) {
            parser.set_error("Unexpected data after JSON value");
            return JsonValue();
        }
    }
    if (error && parser.has_error_) {
        *error = parser.error_ ? *parser.error_ : container::String("parse error");
    }
    return result;
}

JsonParser::JsonParser(std::string_view input)
    : ptr_(input.data())
    , end_(input.data() + input.size())
    , start_(input.data())
    , error_(nullptr)
    , has_error_(false) {}

void JsonParser::skip_whitespace() {
    while (ptr_ < end_) {
        char c = *ptr_;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            ++ptr_;
        } else {
            break;
        }
    }
}

char JsonParser::peek() const {
    return ptr_ < end_ ? *ptr_ : '\0';
}

char JsonParser::advance() {
    return ptr_ < end_ ? *ptr_++ : '\0';
}

bool JsonParser::at_end() const {
    return ptr_ >= end_;
}

bool JsonParser::match(char expected) {
    if (ptr_ < end_ && *ptr_ == expected) {
        ++ptr_;
        return true;
    }
    return false;
}

void JsonParser::set_error(const char* msg) {
    if (!has_error_ && error_) {
        size_t pos = static_cast<size_t>(ptr_ - start_);
        char buf[128];
        snprintf(buf, sizeof(buf), "%s at position %zu", msg, pos);
        *error_ = container::String(buf);
    }
    has_error_ = true;
}

JsonValue JsonParser::error_value() {
    has_error_ = true;
    return JsonValue();
}

JsonValue JsonParser::parse_value() {
    skip_whitespace();
    if (at_end()) {
        set_error("Unexpected end of input");
        return JsonValue();
    }

    char c = peek();
    switch (c) {
    case 'n': return parse_literal("null", 4, JsonValue(nullptr));
    case 't': return parse_literal("true", 4, JsonValue(true));
    case 'f': return parse_literal("false", 5, JsonValue(false));
    case '"': return parse_string();
    case '{': return parse_object();
    case '[': return parse_array();
    case '-':
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
        return parse_number();
    default:
        set_error("Unexpected character");
        return JsonValue();
    }
}

JsonValue JsonParser::parse_literal(const char* expected, size_t len, JsonValue&& result) {
    if (static_cast<size_t>(end_ - ptr_) >= len &&
        std::memcmp(ptr_, expected, len) == 0) {
        ptr_ += len;
        return std::move(result);
    }
    set_error("Invalid literal");
    return JsonValue();
}

JsonValue JsonParser::parse_string() {
    if (!match('"')) {
        set_error("Expected '\"'");
        return JsonValue();
    }

    const char* str_start = ptr_;

    // 快速路径：无转义字符串
    while (ptr_ < end_) {
        char c = *ptr_;
        if (c == '"') {
            size_t len = static_cast<size_t>(ptr_ - str_start);
            ++ptr_;
            // 池化分配字符串
            auto* s = pooled_new_string(str_start, len);
            return JsonValue(s, JsonValue::FLAG_POOLED_STRING);
        }
        if (c == '\\') break;
        if (static_cast<unsigned char>(c) < 0x20) {
            set_error("Control character in string");
            return JsonValue();
        }
        ++ptr_;
    }

    if (ptr_ >= end_) {
        set_error("Unterminated string");
        return JsonValue();
    }

    // 慢路径：处理转义序列
    container::String result;
    result.reserve(static_cast<size_t>(ptr_ - str_start + 16));

    if (ptr_ > str_start) {
        result.append(str_start, static_cast<size_t>(ptr_ - str_start));
    }

    while (ptr_ < end_) {
        char c = *ptr_;
        if (c == '"') {
            ++ptr_;
            auto* owned = pooled_new_string(std::move(result));
            return JsonValue(owned, JsonValue::FLAG_POOLED_STRING);
        }
        if (c == '\\') {
            ++ptr_;
            if (ptr_ >= end_) {
                set_error("Unterminated escape");
                return JsonValue();
            }
            char esc = *ptr_++;
            switch (esc) {
            case '"': result.append('"'); break;
            case '\\': result.append('\\'); break;
            case '/': result.append('/'); break;
            case 'b': result.append('\b'); break;
            case 'f': result.append('\f'); break;
            case 'n': result.append('\n'); break;
            case 'r': result.append('\r'); break;
            case 't': result.append('\t'); break;
            case 'u': {
                if (static_cast<size_t>(end_ - ptr_) < 4) {
                    set_error("Invalid unicode escape");
                    return JsonValue();
                }
                uint32_t cp = 0;
                for (int i = 0; i < 4; ++i) {
                    char hex = ptr_[i];
                    cp <<= 4;
                    if (hex >= '0' && hex <= '9') cp |= hex - '0';
                    else if (hex >= 'a' && hex <= 'f') cp |= hex - 'a' + 10;
                    else if (hex >= 'A' && hex <= 'F') cp |= hex - 'A' + 10;
                    else { set_error("Invalid hex digit"); return JsonValue(); }
                }
                ptr_ += 4;

                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    if (static_cast<size_t>(end_ - ptr_) < 6 ||
                        ptr_[0] != '\\' || ptr_[1] != 'u') {
                        set_error("Missing low surrogate");
                        return JsonValue();
                    }
                    uint32_t lo = 0;
                    for (int i = 2; i < 6; ++i) {
                        char hex = ptr_[i];
                        lo <<= 4;
                        if (hex >= '0' && hex <= '9') lo |= hex - '0';
                        else if (hex >= 'a' && hex <= 'f') lo |= hex - 'a' + 10;
                        else if (hex >= 'A' && hex <= 'F') lo |= hex - 'A' + 10;
                        else { set_error("Invalid hex digit"); return JsonValue(); }
                    }
                    if (lo < 0xDC00 || lo > 0xDFFF) {
                        set_error("Invalid low surrogate");
                        return JsonValue();
                    }
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    ptr_ += 6;
                }

                if (cp <= 0x7F) {
                    result.append(static_cast<char>(cp));
                } else if (cp <= 0x7FF) {
                    result.append(static_cast<char>(0xC0 | (cp >> 6)));
                    result.append(static_cast<char>(0x80 | (cp & 0x3F)));
                } else if (cp <= 0xFFFF) {
                    result.append(static_cast<char>(0xE0 | (cp >> 12)));
                    result.append(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                    result.append(static_cast<char>(0x80 | (cp & 0x3F)));
                } else if (cp <= 0x10FFFF) {
                    result.append(static_cast<char>(0xF0 | (cp >> 18)));
                    result.append(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                    result.append(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                    result.append(static_cast<char>(0x80 | (cp & 0x3F)));
                }
                break;
            }
            default:
                set_error("Invalid escape character");
                return JsonValue();
            }
        } else {
            if (static_cast<unsigned char>(c) < 0x20) {
                set_error("Control character in string");
                return JsonValue();
            }
            result.append(c);
            ++ptr_;
        }
    }

    set_error("Unterminated string");
    return JsonValue();
}

JsonValue JsonParser::parse_number() {
    const char* num_start = ptr_;

    if (peek() == '-') advance();

    if (peek() == '0') {
        advance();
    } else if (peek() >= '1' && peek() <= '9') {
        advance();
        while (ptr_ < end_ && *ptr_ >= '0' && *ptr_ <= '9') ++ptr_;
    } else {
        set_error("Invalid number");
        return JsonValue();
    }

    bool is_float = false;

    if (ptr_ < end_ && *ptr_ == '.') {
        is_float = true;
        ++ptr_;
        if (ptr_ >= end_ || *ptr_ < '0' || *ptr_ > '9') {
            set_error("Invalid number: no digit after '.'");
            return JsonValue();
        }
        while (ptr_ < end_ && *ptr_ >= '0' && *ptr_ <= '9') ++ptr_;
    }

    if (ptr_ < end_ && (*ptr_ == 'e' || *ptr_ == 'E')) {
        is_float = true;
        ++ptr_;
        if (ptr_ < end_ && (*ptr_ == '+' || *ptr_ == '-')) ++ptr_;
        if (ptr_ >= end_ || *ptr_ < '0' || *ptr_ > '9') {
            set_error("Invalid number: no digit in exponent");
            return JsonValue();
        }
        while (ptr_ < end_ && *ptr_ >= '0' && *ptr_ <= '9') ++ptr_;
    }

    if (is_float) {
        char* end_ptr = nullptr;
        double val = std::strtod(num_start, &end_ptr);
        return JsonValue(val);
    }

    if (num_start[0] == '-') {
        char* end_ptr = nullptr;
        int64_t val = std::strtoll(num_start, &end_ptr, 10);
        return JsonValue(val);
    } else {
        char* end_ptr = nullptr;
        uint64_t val = std::strtoull(num_start, &end_ptr, 10);
        return JsonValue(val);
    }
}

JsonValue JsonParser::parse_object() {
    if (!match('{')) {
        set_error("Expected '{'");
        return JsonValue();
    }

    // 池化分配 JsonObject
    auto* obj = pooled_new_object();
    JsonValue result(obj, JsonValue::FLAG_POOLED_OBJECT);

    skip_whitespace();
    if (match('}')) return result;

    while (true) {
        skip_whitespace();

        if (peek() != '"') {
            set_error("Expected string key in object");
            return result;
        }

        auto key_val = parse_string();
        if (has_error_) return result;

        container::String key = key_val.as_string();

        skip_whitespace();
        if (!match(':')) {
            set_error("Expected ':' after key");
            return result;
        }

        auto val = parse_value();
        if (has_error_) return result;

        (*obj)[std::string_view(key.data(), key.size())] = std::move(val);

        skip_whitespace();
        if (match('}')) break;
        if (!match(',')) {
            set_error("Expected ',' or '}' in object");
            return result;
        }
    }

    return result;
}

JsonValue JsonParser::parse_array() {
    if (!match('[')) {
        set_error("Expected '['");
        return JsonValue();
    }

    // 池化分配 JsonArray
    auto* arr = pooled_new_array();
    JsonValue result(arr, JsonValue::FLAG_POOLED_ARRAY);

    skip_whitespace();
    if (match(']')) return result;

    while (true) {
        skip_whitespace();

        auto val = parse_value();
        if (has_error_) return result;

        arr->push_back(std::move(val));

        skip_whitespace();
        if (match(']')) break;
        if (!match(',')) {
            set_error("Expected ',' or ']' in array");
            return result;
        }
    }

    return result;
}

} // namespace ben_gear::base::json
