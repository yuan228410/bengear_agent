#pragma once

#include "ben_gear/base/container/string.hpp"

#include <sstream>
#include <string_view>
#include <type_traits>

namespace ben_gear::base::container {

// ==================== 格式化工具 ====================

/// 将任意类型转换为字符串
template<typename T>
container::String to_string(const T& value) {
    if constexpr (std::is_same_v<T, container::String>) {
        return value;
    } else if constexpr (std::is_same_v<T, std::string>) {
        return container::String(value.c_str());
    } else if constexpr (std::is_same_v<T, std::string_view>) {
        return container::String(value.data(), value.size());
    } else if constexpr (std::is_same_v<T, const char*> || std::is_array_v<T>) {
        return container::String(value);
    } else if constexpr (std::is_arithmetic_v<T>) {
        return container::String(std::to_string(value).c_str());
    } else {
        std::ostringstream oss;
        oss << value;
        return container::String(oss.str().c_str());
    }
}

/// 内部实现：带参数的格式化（至少 1 个参数，保证数组非零）
template<typename... Args>
container::String format_impl(std::string_view fmt, Args&&... args) {
    container::String result;
    result.reserve(fmt.size() * 2);

    size_t arg_index = 0;
    size_t pos = 0;
    constexpr size_t arg_count = sizeof...(Args);
    // 占位元素 [0] 保证数组非零长度，实际参数从 [1] 开始
    container::String args_array[] = { container::String(), std::forward<Args>(args)... };

    while (pos < fmt.size()) {
        size_t start = fmt.find('{', pos);
        if (start == std::string_view::npos) {
            result.append(fmt.data() + pos, fmt.size() - pos);
            break;
        }

        size_t end = fmt.find('}', start);
        if (end == std::string_view::npos) {
            result.append(fmt.data() + pos, fmt.size() - pos);
            break;
        }

        if (start > pos) {
            result.append(fmt.data() + pos, start - pos);
        }

        // 转义 "{{"
        if (start + 1 < fmt.size() && fmt[start + 1] == '{') {
            result.append("{");
            pos = start + 2;
            continue;
        }

        // 替换占位符，参数从 args_array[1] 开始
        if (arg_index < arg_count) {
            result.append(args_array[arg_index + 1]);
            ++arg_index;
        } else {
            result.append(fmt.data() + start, end - start + 1);
        }

        pos = end + 1;
    }

    return result;
}

/// 格式化字符串（C++20 std::format 风格）
/// 支持 {} 占位符，{{ 转义为 {
template<typename... Args>
container::String format(std::string_view fmt, Args&&... args) {
    // 零参数时直接返回原字符串
    if constexpr (sizeof...(Args) == 0) {
        container::String result;
        result.append(fmt.data(), fmt.size());
        return result;
    }

    return format_impl(fmt, to_string(std::forward<Args>(args))...);
}

/// 格式化字符串（类型安全版本）
template<typename... Args>
container::String format_safe(std::string_view fmt, Args&&... args) {
    return format(fmt, std::forward<Args>(args)...);
}

// ==================== 流式格式化 ====================

/// 流式格式化器
class FormatStream {
public:
    FormatStream() = default;

    template<typename T>
    FormatStream& operator<<(const T& value) {
        buffer_ << value;
        return *this;
    }

    container::String str() const {
        return container::String(buffer_.str().c_str());
    }

private:
    std::ostringstream buffer_;
};

/// 创建流式格式化器
inline FormatStream format_stream() {
    return FormatStream();
}

}  // namespace ben_gear::base::container
