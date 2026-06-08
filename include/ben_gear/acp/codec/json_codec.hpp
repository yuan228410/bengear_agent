#pragma once

#include "serializer.hpp"
#include "ben_gear/base/utils/json.hpp"

namespace ben_gear::acp {

// ==================== JSON 序列化器 ====================

class JsonSerializer : public ISerializer {
public:
    /// 序列化消息（高性能：直接使用 Json::dump）
    container::String serialize(const ACPMessage& msg) override {
        return msg.to_json().dump();
    }
    
    /// 序列化内容块
    container::String serialize(const ContentBlock& block) override {
        return block.to_json().dump();
    }
    
    /// 序列化为 Json 对象（零拷贝）
    Json to_json(const ACPMessage& msg) {
        return msg.to_json();
    }
    
    Json to_json(const ContentBlock& block) {
        return block.to_json();
    }
};

// ==================== JSON 解析器 ====================

class JsonParser : public IParser {
public:
    /// 解析消息（高性能：使用 container::Json）
    std::optional<ACPMessage> parse(std::string_view data, 
                                     container::String& error) override {
        try {
            auto json = Json::parse(data, error);
            if (!error.empty()) {
                return std::nullopt;
            }
            return ACPMessage::from_json(json);
        } catch (const std::exception& e) {
            error = container::String(e.what());
            return std::nullopt;
        }
    }
    
    /// 解析内容块
    std::optional<ContentBlock> parse_block(const Json& j, 
                                             container::String& error) override {
        try {
            return ContentBlock::from_json(j);
        } catch (const std::exception& e) {
            error = container::String(e.what());
            return std::nullopt;
        }
    }
    
    /// 从 Json 对象解析（零拷贝）
    ACPMessage from_json(const Json& j) {
        return ACPMessage::from_json(j);
    }
    
    ContentBlock from_json_block(const Json& j) {
        return ContentBlock::from_json(j);
    }
};

} // namespace ben_gear::acp
