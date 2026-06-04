#pragma once

#include "ben_gear/base/log/logger.hpp"

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>

namespace ben_gear {

// 直接使用 nlohmann/json
using Json = nlohmann::json;

// JSON 解析
inline Json parse_json(std::string_view text) {
    return Json::parse(text);
}

inline Json parse_json(std::string_view text, std::string& error) noexcept {
    try {
        return Json::parse(text);
    } catch (const std::exception& e) {
        error = e.what();
        return Json{};
    }
}

// 安全获取值
template <typename T>
std::optional<T> get_json_value(const Json& json, std::string_view key) {
    if (!json.is_object()) {
        return std::nullopt;
    }
    auto it = json.find(key);
    if (it == json.end()) {
        return std::nullopt;
    }
    try {
        return it->get<T>();
    } catch (const std::exception& e) {
        log::error_fmt("get_json_value failed: key={} error={}", key, e.what());
        return std::nullopt;
    }
}

// JSON 字符串转义
inline std::string json_string(std::string_view value) {
    return Json(value).dump();
}

}  // namespace ben_gear
