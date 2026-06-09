#pragma once

#include "ben_gear/base/json/json.hpp"
#include "ben_gear/base/log/logger.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace ben_gear {

using Json = base::container::Json;

/// JSON 解析
inline Json parse_json(std::string_view text) {
    return Json::parse(text);
}

inline Json parse_json(std::string_view text, std::string& error) noexcept {
    try {
        base::container::String err;
        auto result = Json::parse(text, err);
        if (!err.empty()) {
            error = std::string(err.data(), err.size());
        }
        return result;
    } catch (const std::exception& e) {
        error = e.what();
        return Json();
    }
}

/// 安全获取值（零异常开销）
/// - std::string: 用 is_string() 判断替代 try-catch，避免异常开销
/// - bool/int/double: 直接类型检查 + 转换，零异常
/// - 其他类型: try-catch 兜底
template <typename T>
std::optional<T> get_json_value(const Json& json, std::string_view key) {
    if (!json.is_object()) {
        return std::nullopt;
    }
    auto it = json.find(key);
    if (it == json.end()) {
        return std::nullopt;
    }
    // 热点路径优化：std::string 类型用 is_string() 判断，避免 try-catch
    if constexpr (std::is_same_v<T, std::string>) {
        if (!it->is_string()) {
            return std::nullopt;
        }
        auto s = it->as_string();
        return std::string(s.data(), s.size());
    } else if constexpr (std::is_same_v<T, bool>) {
        if (!it->is_boolean()) return std::nullopt;
        return it->get<bool>();
    } else if constexpr (std::is_same_v<T, int>) {
        if (!it->is_number()) return std::nullopt;
        return it->get<int>();
    } else if constexpr (std::is_same_v<T, int64_t>) {
        if (!it->is_number()) return std::nullopt;
        return it->get<int64_t>();
    } else if constexpr (std::is_same_v<T, double>) {
        if (!it->is_number()) return std::nullopt;
        return it->get<double>();
    } else {
        try {
            return it->get<T>();
        } catch (const std::exception& e) {
            log::error_fmt("get_json_value failed: key={} error={}", key, e.what());
            return std::nullopt;
        }
    }
}

/// JSON 字符串转义
inline std::string json_string(std::string_view value) {
    auto result = Json(value).dump();
    return std::string(result.data(), result.size());
}

}  // namespace ben_gear
