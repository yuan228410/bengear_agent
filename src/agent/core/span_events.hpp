#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace ben_gear::agent {

/// Span 开始事件
struct SpanStartEvent {
    uint64_t span_id;
    std::string_view name;
    std::string_view kind;  // "llm", "tool", "sub_agent"
    std::chrono::steady_clock::time_point start_time;
};

/// Span 结束事件
struct SpanEndEvent {
    uint64_t span_id;
    bool error = false;
    std::string_view error_message;
    std::chrono::microseconds duration;
};

} // namespace ben_gear::agent
