#pragma once

#include "base/config/settings.hpp"

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

inline container::String endpoint_url(const config::Settings& settings, std::string_view default_path) {
    std::string url;
    if (!settings.api_url.empty()) {
        url = without_trailing_slash(settings.api_url);
        if (ends_with(url, "/chat/completions") || ends_with(url, "/messages")) {
            return container::String(url.data(), url.size());
        }
        if (ends_with(url, "/v1") && default_path.substr(0, 4) == "/v1/") {
            url += std::string(default_path.substr(3));
            return container::String(url.data(), url.size());
        }
        return container::String(url.data(), url.size());
    }
    url = without_trailing_slash(settings.base_url);
    if (ends_with(url, "/v1") && default_path.substr(0, 4) == "/v1/") {
        url += std::string(default_path.substr(3));
        return container::String(url.data(), url.size());
    }
    url += std::string(default_path);
    return container::String(url.data(), url.size());
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
