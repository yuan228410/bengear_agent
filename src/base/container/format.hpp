#pragma once

#include <array>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace ben_gear::base::container {

// ==================== 格式化工具 ====================

/// 将任意类型转换为字符串（热路径优化：std::string 右值直接移动，避免拷贝）
template<typename T>
std::string to_string(T&& value) {
    using Decayed = std::decay_t<T>;
    if constexpr (std::is_same_v<Decayed, std::string>) {
        return std::forward<T>(value);  // 右值移动，左值拷贝
    } else if constexpr (std::is_same_v<Decayed, std::string_view>) {
        return std::string(value);
    } else if constexpr (std::is_same_v<Decayed, const char*> || std::is_array_v<std::remove_reference_t<T>>) {
        return std::string(value);
    } else if constexpr (std::is_arithmetic_v<Decayed>) {
        return std::to_string(value);
    } else {
        std::ostringstream oss;
        oss << value;
        return oss.str();
    }
}

/// 惰性格式化实现：仅在遇到 {} 占位符时才将对应参数转为字符串，避免无谓分配
template<typename... Args>
std::string format_impl(std::string_view fmt, Args&&... args) {
    constexpr size_t arg_count = sizeof...(Args);

    // 无参数快速路径：仅处理 {{ 转义
    if constexpr (arg_count == 0) {
        std::string result;
        result.reserve(fmt.size());
        size_t pos = 0;
        while (pos < fmt.size()) {
            size_t start = fmt.find('{', pos);
            if (start == std::string_view::npos) {
                result.append(fmt.data() + pos, fmt.size() - pos);
                break;
            }
            if (start > pos) {
                result.append(fmt.data() + pos, start - pos);
            }
            if (start + 1 < fmt.size() && fmt[start + 1] == '{') {
                result.push_back('{');
                pos = start + 2;
            } else {
                result.push_back('{');
                pos = start + 1;
            }
        }
        return result;
    }

    std::string result;
    result.reserve(fmt.size() * 2);

    // 惰性转换缓存：仅在需要时转换对应参数，避免提前 stringify 全部参数
    auto tup = std::forward_as_tuple(std::forward<Args>(args)...);
    std::array<std::string, arg_count> cached;
    std::array<bool, arg_count> converted = {};

    auto get_arg_str = [&](size_t idx) -> const std::string& {
        if (!converted[idx]) {
            converted[idx] = true;
            // 编译期展开为 if-else 链，仅匹配分支执行 to_string，其余返回空临时串（被折叠丢弃）
            [&]<size_t... Is>(std::index_sequence<Is...>) {
                ((Is == idx ? cached[idx] = to_string(std::get<Is>(tup)) : std::string{}), ...);
            }(std::index_sequence_for<Args...>{});
        }
        return cached[idx];
    };

    size_t arg_index = 0;
    size_t pos = 0;

    while (pos < fmt.size()) {
        size_t start = fmt.find('{', pos);
        if (start == std::string_view::npos) {
            result.append(fmt.data() + pos, fmt.size() - pos);
            break;
        }

        if (start > pos) {
            result.append(fmt.data() + pos, start - pos);
        }

        // {{ 转义为 {
        if (start + 1 < fmt.size() && fmt[start + 1] == '{') {
            result.push_back('{');
            pos = start + 2;
            continue;
        }

        size_t end = fmt.find('}', start);
        if (end == std::string_view::npos) {
            // '}' 缺失：保留剩余原文
            result.append(fmt.data() + pos, fmt.size() - pos);
            break;
        }

        if (arg_index < arg_count) {
            result.append(get_arg_str(arg_index));
            ++arg_index;
        } else {
            // 参数不够，保留原始占位符
            result.append(fmt.data() + start, end - start + 1);
        }

        pos = end + 1;
    }

    return result;
}

/// 格式化字符串（C++20 std::format 风格）
/// 支持 {} 占位符，{{ 转义为 {
template<typename... Args>
std::string format(std::string_view fmt, Args&&... args) {
    return format_impl(fmt, std::forward<Args>(args)...);
}

/// 格式化字符串（类型安全版本）
template<typename... Args>
std::string format_safe(std::string_view fmt, Args&&... args) {
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

    std::string str() const { return buffer_.str(); }

private:
    std::ostringstream buffer_;
};

/// 创建流式格式化器
inline FormatStream format_stream() { return FormatStream(); }

}  // namespace ben_gear::base::container
