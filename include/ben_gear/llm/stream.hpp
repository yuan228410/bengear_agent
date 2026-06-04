#pragma once

#include "ben_gear/base/container/string.hpp"

#include <functional>
#include <string_view>

namespace ben_gear::llm {

namespace container = base::container;

using StreamTokenHandler = std::function<void(std::string_view)>;
using StreamThinkingHandler = std::function<void(std::string_view)>;

/// 流式工具调用增量
struct StreamToolCallDelta {
    int index = 0;                          // 工具调用索引
    container::String id;                   // 工具调用 ID（仅首次）
    container::String name;                 // 工具名称（仅首次）
    container::String arguments;            // 参数增量 JSON
};

using StreamToolCallHandler = std::function<void(const StreamToolCallDelta&)>;

/// 流式消息停止原因
struct StreamStopInfo {
    container::String stop_reason;          // "end_turn", "tool_use", "stop"
};

using StreamStopHandler = std::function<void(const StreamStopInfo&)>;

struct StreamHandlers {
    StreamTokenHandler on_token;
    StreamThinkingHandler on_thinking;
    StreamToolCallHandler on_tool_call;
    StreamStopHandler on_stop;

    StreamHandlers() = default;
    StreamHandlers(StreamTokenHandler token, StreamThinkingHandler thinking = {},
                   StreamToolCallHandler tool_call = {}, StreamStopHandler stop = {})
        : on_token(std::move(token)), on_thinking(std::move(thinking)),
          on_tool_call(std::move(tool_call)), on_stop(std::move(stop)) {}
};

struct StreamResult {
    int status = 0;
    container::String raw;
};

}  // namespace ben_gear::llm

namespace ben_gear {
using StreamHandlers = llm::StreamHandlers;
using StreamResult = llm::StreamResult;
using StreamThinkingHandler = llm::StreamThinkingHandler;
using StreamTokenHandler = llm::StreamTokenHandler;
using StreamToolCallDelta = llm::StreamToolCallDelta;
using StreamStopInfo = llm::StreamStopInfo;
}  // namespace ben_gear
