#pragma once

#include "base/config/settings.hpp"
#include <vector>

#include <string>
#include <string_view>

namespace ben_gear::llm {

inline std::string without_trailing_slash(const std::string& value) {
    auto result = value;
    while (!result.empty() && result.back() == '/') {
        result.pop_back();
    }
    return result;
}

inline bool ends_with(std::string_view value, std::string_view suffix) {
    return value.size() >= suffix.size() && value.substr(value.size() - suffix.size()) == suffix;
}

inline std::string endpoint_url(const config::Settings& settings, std::string_view default_path) {
    std::string base;
    bool has_custom_api = !settings.api_url.empty();
    if (has_custom_api) {
        base = without_trailing_slash(settings.api_url);
    } else {
        base = without_trailing_slash(settings.base_url);
    }

    if (ends_with(base, "/chat/completions") || ends_with(base, "/messages")) {
        return base;
    }

    if (has_custom_api) {
        auto scheme_end = base.find("://");
        if (scheme_end != std::string::npos) {
            auto path_start = base.find('/', scheme_end + 3);
            if (path_start != std::string::npos) {
                std::string_view path_part(base.data() + path_start, base.size() - path_start);
                if (path_part != "/v1") return base;
            }
        }
    }

    if (ends_with(base, "/v1") && default_path.size() > 3 && default_path.substr(0, 4) == "/v1/") {
        base += std::string(default_path.substr(3));
        return base;
    }
    base += std::string(default_path);
    return base;
}

inline std::vector<std::string> custom_headers(const config::Settings& settings) {
    std::vector<std::string> headers;
    headers.reserve(settings.headers.size());
    for (const auto& [key, value] : settings.headers) {
        headers.push_back(key + std::string(": ") + value);
    }
    return headers;
}

}  // namespace ben_gear::llm
