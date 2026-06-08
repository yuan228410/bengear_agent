#include "ben_gear/acp/core/message.hpp"

namespace ben_gear::acp {

// 使用命名空间别名简化代码
namespace container = base::container;

// ==================== ACPMessage 序列化 ====================

Json ACPMessage::to_json() const {
    Json j;
    j["type"] = "message";
    j["role"] = role_to_string(role_);
    
    // 内容块
    j["content"] = Json::array();
    for (const auto& block : content_) {
        j["content"].push_back(block.to_json());
    }
    
    return j;
}

ACPMessage ACPMessage::from_json(const Json& j) {
    ACPMessage msg;
    
    // 角色
    auto role_str = j.value("role", "user");
    msg.role_ = string_to_role(role_str);
    
    // 内容块
    if (j.contains("content") && j["content"].is_array()) {
        for (const auto& block_json : j["content"]) {
            msg.content_.push_back(ContentBlock::from_json(block_json));
        }
    }
    
    return msg;
}

} // namespace ben_gear::acp
