#pragma once

#include "base/config/settings.hpp"
#include "base/container/string.hpp"
#include "base/container/vector.hpp"

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

inline container::String endpoint_url(const config::Settings& settings, std::string_view default_path) {
    std::string base;
    if (!settings.api_url.empty()) {
        base = without_trailing_slash(std::string(settings.api_url.data(), settings.api_url.size()));
    } else {
        base = without_trailing_slash(std::string(settings.base_url.data(), settings.base_url.size()));
    }

    if (ends_with(base, "/chat/completions") || ends_with(base, "/messages")) {
        return container::String(base.data(), base.size());
    }
    if (ends_with(base, "/v1") && default_path.size() > 3 && default_path.substr(0, 4) == "/v1/") {
        base += std::string(default_path.substr(3));
        return container::String(base.data(), base.size());
    }
    base += std::string(default_path);
    return container::String(base.data(), base.size());
}

inline container::Vector<container::String> custom_headers(const config::Settings& settings) {
    container::Vector<container::String> headers;
    headers.reserve(settings.headers.size());
    for (const auto& [key, value] : settings.headers) {
        headers.push_back(container::String(key.c_str()) + container::String(": ") + container::String(value.c_str()));
    }
    return headers;
}

}  // namespace ben_gear::llm
