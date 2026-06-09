#include "ben_gear/acp/core/content_block.hpp"

namespace ben_gear::acp {

// ==================== 序列化实现 ====================

Json ContentBlock::to_json() const {
    Json j;
    
    std::visit([&j](const auto& content) {
        using T = std::decay_t<decltype(content)>;
        
        if constexpr (std::is_same_v<T, TextContent>) {
            if (content.is_thinking) {
                j["type"] = "thinking";
                j["thinking"] = content.text;
            } else {
                j["type"] = "text";
                j["text"] = content.text;
            }
        }
        else if constexpr (std::is_same_v<T, MediaContent>) {
            switch (content.type) {
                case ContentType::Image:
                    j["type"] = "image";
                    break;
                case ContentType::Audio:
                    j["type"] = "audio";
                    break;
                case ContentType::Video:
                    j["type"] = "video";
                    break;
                default:
                    j["type"] = "unknown";
            }
            j["source"] = content.source.to_json();
        }
        else if constexpr (std::is_same_v<T, ToolUseContent>) {
            j["type"] = "tool_use";
            j["id"] = content.call.id;
            j["name"] = content.call.name;
            j["input"] = content.call.arguments;
        }
        else if constexpr (std::is_same_v<T, ToolResultContent>) {
            j["type"] = "tool_result";
            j["tool_use_id"] = content.result.tool_call_id;
            j["content"] = content.result.output;
        }
    }, content_);
    
    return j;
}

ContentBlock ContentBlock::from_json(const Json& j) {
    auto type_str = j.value("type", "text");
    
    if (type_str == "text") {
        return ContentBlock::text(
            j.value("text", "")
        );
    }
    else if (type_str == "thinking") {
        return ContentBlock::thinking(
            j.value("thinking", "")
        );
    }
    else if (type_str == "image") {
        auto source = Source::from_json(j["source"]);
        return ContentBlock::image(std::move(source));
    }
    else if (type_str == "audio") {
        auto source = Source::from_json(j["source"]);
        return ContentBlock::audio(std::move(source));
    }
    else if (type_str == "video") {
        auto source = Source::from_json(j["source"]);
        return ContentBlock::video(std::move(source));
    }
    else if (type_str == "tool_use") {
        llm::ToolCallRequest call;
        call.id = j.value("id", "");
        call.name = j.value("name", "");
        call.arguments = j.value("input", Json::object());
        return ContentBlock::tool_use(std::move(call));
    }
    else if (type_str == "tool_result") {
        llm::ToolCallResult result;
        result.tool_call_id = j.value("tool_use_id", "");
        result.output = j.value("content", "");
        return ContentBlock::tool_result(std::move(result));
    }
    
    // 默认返回空文本
    return ContentBlock::text(container::String());
}

} // namespace ben_gear::acp
