#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace ben_gear::base::utils {

inline std::string trim(std::string_view value) {
    auto begin = value.begin();
    auto end = value.end();
    while (begin != end && std::isspace(static_cast<unsigned char>(*begin))) {
        ++begin;
    }
    while (begin != end && std::isspace(static_cast<unsigned char>(*(end - 1)))) {
        --end;
    }
    return {begin, end};
}

inline std::string to_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

}  // namespace ben_gear::base::utils

// 向 ben_gear 顶层导出，保持调用方便
namespace ben_gear {
using base::utils::to_lower;
using base::utils::trim;
}  // namespace ben_gear
