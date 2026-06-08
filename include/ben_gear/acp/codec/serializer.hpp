#pragma once

#include "ben_gear/acp/core/message.hpp"
#include "ben_gear/base/container/string.hpp"

namespace ben_gear::acp {

// ==================== 序列化器接口 ====================

class ISerializer {
public:
    virtual ~ISerializer() = default;
    
    /// 序列化消息
    virtual container::String serialize(const ACPMessage& msg) = 0;
    
    /// 序列化内容块
    virtual container::String serialize(const ContentBlock& block) = 0;
};

// ==================== 解析器接口 ====================

class IParser {
public:
    virtual ~IParser() = default;
    
    /// 解析消息
    virtual std::optional<ACPMessage> parse(std::string_view data, 
                                             container::String& error) = 0;
    
    /// 解析内容块
    virtual std::optional<ContentBlock> parse_block(const Json& j, 
                                                     container::String& error) = 0;
};

} // namespace ben_gear::acp
