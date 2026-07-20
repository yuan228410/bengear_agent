#pragma once

#include "llm/usage.hpp"

#include <functional>
#include <memory>
#include <string_view>

namespace ben_gear::llm {


using StreamTokenHandler = std::function<void(std::string_view)>;
using StreamThinkingHandler = std::function<void(std::string_view)>;

/// 流式工具调用增量
struct StreamToolCallDelta {
 int index = 0; ///< 工具调用索引
 std::string id; ///< 工具调用 ID（仅首次）
 std::string name; ///< 工具名称（仅首次）
 std::string arguments; ///< 参数增量 JSON
};

using StreamToolCallHandler = std::function<void(const StreamToolCallDelta&)>;

/// 流式消息停止原因（回调参数）
struct StreamStopInfo {
 std::string stop_reason; ///< "end_turn", "tool_use", "stop"
};

using StreamStopHandler = std::function<void(const StreamStopInfo&)>;

/// 流式解析器停止原因（内部使用）
enum class StreamStopReason {
 none, ///< 未停止
 done, ///< 收到 [DONE] 标记
 finish_stop, ///< finish_reason: stop
 finish_tools, ///< finish_reason: tool_calls
 error ///< 解析错误
};

/// 流式回调集合
///
/// usage_out: 解析器写入 token 用量的共享指针
/// ProviderClient 创建 → 解析器写入 → ProviderClient 读取
/// 三层解耦，不依赖 raw body 二次解析
struct StreamHandlers {
 StreamTokenHandler on_token;
 StreamThinkingHandler on_thinking;
 StreamToolCallHandler on_tool_call;
 StreamStopHandler on_stop;
  std::shared_ptr<TokenUsage> usage_out; ///< 解析器写入，调用方读取（nullptr 时不追踪）

  StreamHandlers() = default;
 StreamHandlers(StreamTokenHandler token, StreamThinkingHandler thinking = {},
  StreamToolCallHandler tool_call = {}, StreamStopHandler stop = {})
  : on_token(std::move(token)), on_thinking(std::move(thinking)),
   on_tool_call(std::move(tool_call)), on_stop(std::move(stop)) {}

 StreamHandlers(const StreamHandlers&) = delete;
 StreamHandlers& operator=(const StreamHandlers&) = delete;
 StreamHandlers(StreamHandlers&&) noexcept = default;
 StreamHandlers& operator=(StreamHandlers&&) noexcept = default;
};

/// 流式响应结果
struct StreamResult {
 int status = 0;
 std::string raw;
 TokenUsage usage;        ///< 从流式响应提取的 token 用量
 RequestLatency latency;  ///< 请求延迟（含 TTFB）
 bool is_context_overflow = false;  ///< 上下文超限标记
};

} // namespace ben_gear::llm

namespace ben_gear {
using StreamHandlers = llm::StreamHandlers;
using StreamResult = llm::StreamResult;
using StreamThinkingHandler = llm::StreamThinkingHandler;
using StreamTokenHandler = llm::StreamTokenHandler;
using StreamToolCallDelta = llm::StreamToolCallDelta;
using StreamStopInfo = llm::StreamStopInfo;
using StreamStopReason = llm::StreamStopReason;
} // namespace ben_gear
