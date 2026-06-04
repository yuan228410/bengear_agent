#pragma once

#include "ben_gear/llm/internal/sse.hpp"
#include "ben_gear/llm/stream.hpp"
#include "ben_gear/base/utils/json.hpp"
#include "ben_gear/base/log/logger.hpp"

#include <string_view>
#include <utility>

namespace ben_gear::llm {

class AnthropicStreamParser {
public:
    explicit AnthropicStreamParser(StreamTokenHandler on_token) : handlers_(std::move(on_token)) {}
    explicit AnthropicStreamParser(StreamHandlers handlers) : handlers_(std::move(handlers)) {}

    void parse(std::string_view payload) const {
        for (const auto& event : parse_sse_events(payload)) {
            if (event.event.empty() || event.event == "content_block_start" ||
                event.event == "content_block_delta" || event.event == "message_delta" ||
                event.event == "message_start" || event.event == "message_stop") {
                // pass
            } else {
                continue;
            }

            std::string error;
            auto root = parse_json(std::string_view(event.data), error);
            if (!error.empty()) {
                log::error_fmt("anthropic sse parse json failed: error={} data_len={}", error, event.data.size());
                continue;
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
    }

private:
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
            // 兼容无 type 字段的旧格式
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

    void handle_message_delta(const Json& root) const {
        if (!handlers_.on_stop) return;
        auto delta = root.find("delta");
        if (delta == root.end() || !delta->is_object()) return;
        if (auto reason = get_json_value<std::string>(*delta, "stop_reason")) {
            handlers_.on_stop(StreamStopInfo{*reason});
        }
    }

    StreamHandlers handlers_;
};

}  // namespace ben_gear::llm
