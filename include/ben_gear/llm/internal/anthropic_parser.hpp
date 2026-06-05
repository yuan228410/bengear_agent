#pragma once

#include "ben_gear/llm/internal/sse.hpp"
#include "ben_gear/llm/stream.hpp"
#include "ben_gear/base/utils/json.hpp"
#include "ben_gear/base/log/logger.hpp"

#include <string_view>
#include <utility>

namespace ben_gear::llm {

/// Anthropic 流式响应解析器
/// 
/// 解析 Anthropic 格式的 SSE 流式响应，支持：
/// - 文本内容增量（text_delta）
/// - 思考内容增量（thinking_delta）
/// - 工具调用增量（tool_use、input_json_delta）
/// - 停止原因检测（stop_reason）
/// 
/// 使用示例：
/// @code
/// AnthropicStreamParser parser(handlers);
/// parser.parse(chunk);
/// if (parser.stopped()) {
///     auto reason = parser.stop_reason();
///     // 处理停止逻辑
/// }
/// @endcode
class AnthropicStreamParser {
public:
    explicit AnthropicStreamParser(StreamTokenHandler on_token) : handlers_(std::move(on_token)) {}
    explicit AnthropicStreamParser(StreamHandlers handlers) : handlers_(std::move(handlers)) {}

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
        if (!event.event.empty() && event.event != "content_block_start" &&
            event.event != "content_block_delta" && event.event != "message_delta" &&
            event.event != "message_start" && event.event != "message_stop") {
            return;
        }

        if (event.data.empty()) return;

        std::string error;
        auto root = parse_json(std::string_view(event.data), error);
        if (!error.empty()) {
            log::error_fmt("anthropic sse parse json failed: error={} data_len={}", error, event.data.size());
            return;
        }

        if (event.event == "content_block_start" || event.event == "") {
            handle_content_block_start(root);
        }

        if (event.event == "content_block_delta" || event.event == "") {
            handle_content_block_delta(root);
        }

        if (event.event == "message_delta" || event.event == "") {
            handle_message_delta(root);
        }
    }

    void handle_content_block_start(const Json& root) const {
        auto cb = root.find("content_block");
        if (cb == root.end() || !cb->is_object()) return;

        auto type = get_json_value<std::string>(*cb, "type");
        if (!type) return;

        if (*type == "tool_use" && handlers_.on_tool_call) {
            StreamToolCallDelta delta;
            auto idx = root.find("index");
            if (idx != root.end() && idx->is_number()) {
                delta.index = idx->get<int>();
            }
            auto id = get_json_value<std::string>(*cb, "id");
            if (id) delta.id = *id;
            auto name = get_json_value<std::string>(*cb, "name");
            if (name) delta.name = *name;
            handlers_.on_tool_call(delta);
        }
    }

    void handle_content_block_delta(const Json& root) const {
        auto delta = root.find("delta");
        if (delta == root.end() || !delta->is_object()) return;

        auto type = get_json_value<std::string>(*delta, "type");
        if (!type) {
            if (handlers_.on_thinking) {
                if (auto thinking = get_json_value<std::string>(*delta, "thinking")) {
                    handlers_.on_thinking(*thinking);
                } else if (auto thinking = get_json_value<std::string>(*delta, "thinking_delta")) {
                    handlers_.on_thinking(*thinking);
                }
            }
            if (handlers_.on_token) {
                if (auto text = get_json_value<std::string>(*delta, "text")) {
                    handlers_.on_token(*text);
                }
            }
            return;
        }

        if (*type == "thinking_delta") {
            if (handlers_.on_thinking) {
                if (auto thinking = get_json_value<std::string>(*delta, "thinking")) {
                    handlers_.on_thinking(*thinking);
                }
            }
        } else if (*type == "text_delta") {
            if (handlers_.on_token) {
                if (auto text = get_json_value<std::string>(*delta, "text")) {
                    handlers_.on_token(*text);
                }
            }
        } else if (*type == "input_json_delta") {
            if (handlers_.on_tool_call) {
                StreamToolCallDelta d;
                auto idx = root.find("index");
                if (idx != root.end() && idx->is_number()) {
                    d.index = idx->get<int>();
                }
                if (auto args = get_json_value<std::string>(*delta, "partial_json")) {
                    d.arguments = *args;
                }
                handlers_.on_tool_call(d);
            }
        }
    }

    void handle_message_delta(const Json& root) {
        if (!handlers_.on_stop) return;
        auto delta = root.find("delta");
        if (delta == root.end() || !delta->is_object()) return;
        if (auto reason = get_json_value<std::string>(*delta, "stop_reason")) {
            handlers_.on_stop(StreamStopInfo{*reason});
            // Anthropic 的 stop_reason 可能是 "end_turn" 或 "tool_use"
            stop_reason_ = (*reason == "tool_use") ? StreamStopReason::finish_tools 
                                                   : StreamStopReason::finish_stop;
        }
    }

    StreamHandlers handlers_;
    SseBuffer sse_buffer_;
    StreamStopReason stop_reason_ = StreamStopReason::none;
};

}  // namespace ben_gear::llm
