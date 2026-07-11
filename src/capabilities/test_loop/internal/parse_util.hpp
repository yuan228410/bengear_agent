#pragma once

// test_loop 内部共享解析辅助：原在多个 .cpp 的匿名命名空间重复定义，
// Unity Build 合并编译单元时会冲突，统一改为 inline 单一定义点。

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace ben_gear::test_loop {

inline std::string trim(std::string value) {
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

inline std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

inline std::vector<std::string> split_lines(std::string_view text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < text.size()) {
        auto end = text.find('\n', start);
        if (end == std::string_view::npos) {
            lines.emplace_back(text.substr(start));
            break;
        }
        lines.emplace_back(text.substr(start, end - start));
        start = end + 1;
    }
    return lines;
}

inline bool looks_like_failure_line(const std::string& line) {
    auto lower = lower_copy(line);
    const char* patterns[] = {
        " fail", "failed", "failure", "error:", "fatal:", "exception", "assert", "undefined reference",
        "no such file", "not found", "segmentation fault", "traceback", "expected", "actual"
    };
    for (const auto* pattern : patterns) {
        if (lower.find(pattern) != std::string::npos) return true;
    }
    return false;
}

}  // namespace ben_gear::test_loop
