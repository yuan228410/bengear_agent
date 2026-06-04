#pragma once

#include "ben_gear/config/settings.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace ben_gear::llm {

inline std::string without_trailing_slash(std::string value) {
    while (!value.empty() && value.back() == '/') {
        value.pop_back();
    }
    return value;
}

inline bool ends_with(std::string_view value, std::string_view suffix) {
    return value.size() >= suffix.size() && value.substr(value.size() - suffix.size()) == suffix;
}

inline std::string endpoint_url(const config::Settings& settings, std::string_view default_path) {
    std::string url;
    if (!settings.api_url.empty()) {
        url = without_trailing_slash(settings.api_url);
        // api_url 已经包含完整路径，直接返回
        if (ends_with(url, "/chat/completions") || ends_with(url, "/messages")) {
            return url;
        }
        // api_url 以 /v1 结尾，补全协议路径
        if (ends_with(url, "/v1") && default_path.substr(0, 4) == "/v1/") {
            return url + std::string(default_path.substr(3));
        }
        // 其他自定义 api_url，直接返回
        return url;
    }
    url = without_trailing_slash(settings.base_url);
    if (ends_with(url, "/v1") && default_path.substr(0, 4) == "/v1/") {
        return url + std::string(default_path.substr(3));
    }
    return url + std::string(default_path);
}

inline std::vector<std::string> custom_headers(const config::Settings& settings) {
    std::vector<std::string> headers;
    headers.reserve(settings.headers.size());
    for (const auto& [key, value] : settings.headers) {
        headers.push_back(key + ": " + value);
    }
    return headers;
}

}  // namespace ben_gear::llm
