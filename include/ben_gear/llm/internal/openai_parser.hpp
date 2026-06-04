#pragma once

#include "ben_gear/llm/internal/sse.hpp"
#include "ben_gear/llm/stream.hpp"
#include "ben_gear/base/utils/json.hpp"
#include "ben_gear/base/log/logger.hpp"

#include <string_view>
#include <utility>

namespace ben_gear::llm {

class OpenAiStreamParser {
public:
    explicit OpenAiStreamParser(StreamTokenHandler on_token) : handlers_(std::move(on_token)) {}
    explicit OpenAiStreamParser(StreamHandlers handlers) : handlers_(std::move(handlers)) {}

    void parse(std::string_view payload) const {
        for (const auto& event : parse_sse_events(payload)) {
            if (event.data == "[DONE]") {
                if (handlers_.on_stop) {
                    handlers_.on_stop(StreamStopInfo{"stop"});
                }
                continue;
            }
            // 解析嵌套的 JSON 结构: {"choices":[{"delta":{"content":"..."}}]}
            std::string error;
            auto root = parse_json(event.data, error);
            if (!error.empty()) {
                log::error_fmt("openai sse parse json failed: error={} data_len={}", error, event.data.size());
                continue;
            }

            auto choices = root.find("choices");
            if (choices == root.end() || !choices->is_array() || choices->empty()) {
                continue;
            }

            auto& first_choice = (*choices)[0];
            auto delta = first_choice.find("delta");
            if (delta == first_choice.end() || !delta->is_object()) {
                continue;
            }

            // 检查 finish_reason（中间块为 null，只有最后一个块有值）
            if (first_choice.contains("finish_reason") && !first_choice["finish_reason"].is_null()) {
                if (auto reason = get_json_value<std::string>(first_choice, "finish_reason")) {
                    if (*reason == "tool_calls" || *reason == "stop") {
                        if (handlers_.on_stop) {
                            handlers_.on_stop(StreamStopInfo{*reason});
                        }
                    }
                }
            }

            // 处理工具调用增量
            if (delta->contains("tool_calls") && (*delta)["tool_calls"].is_array()) {
                if (handlers_.on_tool_call) {
                    for (const auto& tc : (*delta)["tool_calls"]) {
                        StreamToolCallDelta d;
                        d.index = tc.value("index", 0);
                        if (auto id = get_json_value<std::string>(tc, "id")) {
                            d.id = *id;
                        }
                        if (tc.contains("function")) {
                            auto& fn = tc["function"];
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

            // DeepSeek 模型处理：
            // - reasoning_content 总是思考过程，显示在 [thinking] 中
            // - content 是正文，正常显示
            // - 需要显式检查 null 值

            // 提取 reasoning_content（思考过程）- 检查非 null
            if (delta->contains("reasoning_content") && !(*delta)["reasoning_content"].is_null()) {
                if (handlers_.on_thinking) {
                    if (auto thinking = get_json_value<std::string>(*delta, "reasoning_content")) {
                        handlers_.on_thinking(*thinking);
                    }
                }
            }

            // 提取 content（正文）- 检查非 null
            if (delta->contains("content") && !(*delta)["content"].is_null()) {
                if (handlers_.on_token) {
                    if (auto content = get_json_value<std::string>(*delta, "content")) {
                        handlers_.on_token(*content);
                    }
                }
            }
        }
    }

private:
    StreamHandlers handlers_;
};

}  // namespace ben_gear::llm
