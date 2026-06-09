#pragma once

#include "ben_gear/llm/internal/sse.hpp"
#include "ben_gear/llm/stream.hpp"
#include "ben_gear/base/utils/json.hpp"
#include "ben_gear/base/log/logger.hpp"

#include <string_view>
#include <utility>

namespace ben_gear::llm {

/// Anthropic 流式响应解析器
class AnthropicStreamParser {
public:
    explicit AnthropicStreamParser(StreamTokenHandler on_token) : handlers_(std::move(on_token)) {}
    explicit AnthropicStreamParser(StreamHandlers handlers) : handlers_(std::move(handlers)) {}

    void parse(std::string_view payload) {
        sse_buffer_.feed(payload, [&](SseEvent&& event) {
            handle_event(event);
        });
    }

    void finish() {
        sse_buffer_.finish([&](SseEvent&& event) {
            handle_event(event);
        });
    }

    StreamStopReason stop_reason() const { return stop_reason_; }
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

    /// 从 JSON 节点安全提取 string_view（零拷贝，避免临时 std::string）
    /// 返回空 optional 如果 key 不存在或值不是字符串
    static std::optional<std::string> extract_string(const Json& json, std::string_view key) {
        if (!json.contains(key) || !json[key].is_string()) return std::nullopt;
        auto s = json[key].as_string();
        return std::string(s.data(), s.size());
    }

    void handle_content_block_start(const Json& root) const {
        if (!root.contains("content_block") || !root["content_block"].is_object()) return;
        auto cb = root["content_block"];

        auto type = extract_string(cb, "type");
        if (!type) return;

        if (*type == "tool_use" && handlers_.on_tool_call) {
            StreamToolCallDelta delta;
            if (root.contains("index") && root["index"].is_number()) {
                delta.index = root["index"].get<int>();
            }
            auto id = extract_string(cb, "id");
            if (id) delta.id = *id;
            auto name = extract_string(cb, "name");
            if (name) delta.name = *name;
            handlers_.on_tool_call(delta);
        }
    }

    void handle_content_block_delta(const Json& root) const {
        if (!root.contains("delta") || !root["delta"].is_object()) return;
        auto delta = root["delta"];

        auto type = extract_string(delta, "type");
        if (!type) {
            // 无 type 字段：尝试 thinking 和 text
            if (handlers_.on_thinking) {
                auto thinking = extract_string(delta, "thinking");
                if (!thinking) thinking = extract_string(delta, "thinking_delta");
                if (thinking) handlers_.on_thinking(*thinking);
            }
            if (handlers_.on_token) {
                auto text = extract_string(delta, "text");
                if (text) handlers_.on_token(*text);
            }
            return;
        }

        if (*type == "thinking_delta") {
            if (handlers_.on_thinking) {
                auto thinking = extract_string(delta, "thinking");
                if (thinking) handlers_.on_thinking(*thinking);
            }
        } else if (*type == "text_delta") {
            if (handlers_.on_token) {
                auto text = extract_string(delta, "text");
                if (text) handlers_.on_token(*text);
            }
        } else if (*type == "input_json_delta") {
            if (handlers_.on_tool_call) {
                StreamToolCallDelta d;
                if (root.contains("index") && root["index"].is_number()) {
                    d.index = root["index"].get<int>();
                }
                auto args = extract_string(delta, "partial_json");
                if (args) d.arguments = *args;
                handlers_.on_tool_call(d);
            }
        }
    }

    void handle_message_delta(const Json& root) {
        if (!handlers_.on_stop) return;
        if (!root.contains("delta") || !root["delta"].is_object()) return;
        auto delta = root["delta"];
        auto reason = extract_string(delta, "stop_reason");
        if (reason) {
            handlers_.on_stop(StreamStopInfo{*reason});
            stop_reason_ = (*reason == "tool_use") ? StreamStopReason::finish_tools 
                                                   : StreamStopReason::finish_stop;
        }
    }

    StreamHandlers handlers_;
    SseBuffer sse_buffer_;
    StreamStopReason stop_reason_ = StreamStopReason::none;
};

}  // namespace ben_gear::llm
