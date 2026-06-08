#pragma once

#include "ben_gear/llm/internal/sse.hpp"
#include "ben_gear/llm/stream.hpp"
#include "ben_gear/base/utils/json.hpp"
#include "ben_gear/base/log/logger.hpp"

#include <string_view>
#include <utility>

namespace ben_gear::llm {

/// OpenAI 流式响应解析器
/// 
/// 解析 OpenAI 格式的 SSE 流式响应，支持：
/// - 文本内容增量（content）
/// - 思考内容增量（reasoning_content）
/// - 工具调用增量（tool_calls）
/// - 停止原因检测（[DONE]、finish_reason）
/// 
/// 使用示例：
/// @code
/// OpenAiStreamParser parser(handlers);
/// parser.parse(chunk);
/// if (parser.stopped()) {
///     auto reason = parser.stop_reason();
///     // 处理停止逻辑
/// }
/// @endcode
class OpenAiStreamParser {
public:
    explicit OpenAiStreamParser(StreamTokenHandler on_token) : handlers_(std::move(on_token)) {}
    explicit OpenAiStreamParser(StreamHandlers handlers) : handlers_(std::move(handlers)) {}

    /// 解析 SSE 数据块
    /// @param payload SSE 数据块
    void parse(std::string_view payload) {
        sse_buffer_.feed(payload, [&](SseEvent&& event) {
            handle_event(event);
        });
    }

    /// 完成解析（处理缓冲区中剩余数据）
    void finish() {
        sse_buffer_.finish([&](SseEvent&& event) {
            handle_event(event);
        });
    }

    /// 获取停止原因
    /// @return 停止原因枚举值
    StreamStopReason stop_reason() const { return stop_reason_; }
    
    /// 是否已停止
    /// @return true 表示流已结束
    bool stopped() const { return stop_reason_ != StreamStopReason::none; }

private:
    void handle_event(const SseEvent& event) {
        if (event.data == "[DONE]") {
            if (handlers_.on_stop) {
                handlers_.on_stop(StreamStopInfo{"stop"});
            }
            stop_reason_ = StreamStopReason::done;
            return;
        }
        if (event.data.empty()) return;

        std::string error;
        auto root = parse_json(event.data, error);
        if (!error.empty()) {
            log::error_fmt("openai sse parse json failed: error={} data_len={}", error, event.data.size());
            return;
        }

        auto choices = root.find("choices");
        if (choices == root.end() || !choices->is_array() || choices->empty()) {
            return;
        }

        auto first_choice = (*choices)[0];
        auto delta = first_choice.find("delta");
        if (delta == first_choice.end() || !delta->is_object()) {
            return;
        }

        if (first_choice.contains("finish_reason") && !first_choice["finish_reason"].is_null()) {
            if (auto reason = get_json_value<std::string>(first_choice, "finish_reason")) {
                if (*reason == "tool_calls") {
                    if (handlers_.on_stop) {
                        handlers_.on_stop(StreamStopInfo{*reason});
                    }
                    stop_reason_ = StreamStopReason::finish_tools;
                } else if (*reason == "stop") {
                    if (handlers_.on_stop) {
                        handlers_.on_stop(StreamStopInfo{*reason});
                    }
                    stop_reason_ = StreamStopReason::finish_stop;
                }
            }
        }

        if (delta->contains("tool_calls") && (*delta)["tool_calls"].is_array()) {
            if (handlers_.on_tool_call) {
                for (auto tc : (*delta)["tool_calls"]) {
                    StreamToolCallDelta d;
                    d.index = tc.value("index", 0);
                    if (auto id = get_json_value<std::string>(tc, "id")) {
                        d.id = *id;
                    }
                    if (tc.contains("function")) {
                        auto fn = tc["function"];
                        if (auto name = get_json_value<std::string>(fn, "name")) {
                            d.name = *name;
                        }
                        if (auto args = get_json_value<std::string>(fn, "arguments")) {
                            d.arguments = *args;
                        }
                    }
                    handlers_.on_tool_call(d);
                }
            }
        }

        bool has_thinking = delta->contains("reasoning_content") && !(*delta)["reasoning_content"].is_null();
        bool has_content = delta->contains("content") && !(*delta)["content"].is_null();

        if (has_thinking) {
            if (handlers_.on_thinking) {
                if (auto thinking = get_json_value<std::string>(*delta, "reasoning_content")) {
                    handlers_.on_thinking(*thinking);
                }
            }
        } else if (has_content) {
            if (handlers_.on_token) {
                if (auto content = get_json_value<std::string>(*delta, "content")) {
                    handlers_.on_token(*content);
                }
            }
        }
    }

    StreamHandlers handlers_;
    SseBuffer sse_buffer_;
    StreamStopReason stop_reason_ = StreamStopReason::none;
};

}  // namespace ben_gear::llm
