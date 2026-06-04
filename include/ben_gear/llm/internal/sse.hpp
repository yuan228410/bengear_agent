#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/vector.hpp"

#include <string_view>

namespace ben_gear::llm {

// 使用命名空间别名简化代码
namespace container = base::container;

struct SseEvent {
    container::String event;
    container::String data;
};

inline container::String trim_cr(container::String value) {
    if (!value.empty() && value.back() == '\r') {
        value = value.substr(0, value.size() - 1);
    }
    return value;
}

inline container::Vector<SseEvent> parse_sse_events(std::string_view payload) {
    container::Vector<SseEvent> events;
    SseEvent current;
    std::size_t begin = 0;

    auto flush = [&] {
        if (!current.event.empty() || !current.data.empty()) {
            if (!current.data.empty() && current.data.back() == '\n') {
                current.data = current.data.substr(0, current.data.size() - 1);
            }
            events.push_back(std::move(current));
            current = SseEvent{};
        }
    };

    while (begin <= payload.size()) {
        auto end = payload.find('\n', begin);
        auto line = end == std::string_view::npos ? payload.substr(begin) : payload.substr(begin, end - begin);
        auto clean = trim_cr(container::String(line.data(), line.size()));
        if (clean.empty()) {
            flush();
        } else if (std::string_view(clean).substr(0, 6) == "event:") {
            auto value = clean.substr(6);
            if (!value.empty() && value.front() == ' ') {
                value = value.substr(1);
            }
            current.event = std::move(value);
        } else if (std::string_view(clean).substr(0, 5) == "data:") {
            auto value = clean.substr(5);
            if (!value.empty() && value.front() == ' ') {
                value = value.substr(1);
            }
            current.data += value;
            current.data += '\n';
        }
        if (end == std::string_view::npos) {
            break;
        }
        begin = end + 1;
    }
    flush();
    return events;
}

}  // namespace ben_gear::llm
