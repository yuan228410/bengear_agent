#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/vector.hpp"
#include "ben_gear/base/utils/json.hpp"

#include <cstring>

namespace ben_gear::acp {

// ==================== 枚举类型 ====================

/// 消息角色
enum class Role : uint8_t {
    User,
    Assistant,
    System,
    Tool
};

/// 内容块类型
enum class ContentType : uint8_t {
    Text,
    Image,
    Audio,
    Video,
    ToolUse,
    ToolResult,
    Thinking
};

/// 数据源类型
enum class SourceType : uint8_t {
    Base64,
    Url,
    File
};

// ==================== 工具函数 ====================

/// Role 转字符串（高性能：返回静态字符串）
inline const char* role_to_string(Role role) noexcept {
    switch (role) {
        case Role::User: return "user";
        case Role::Assistant: return "assistant";
        case Role::System: return "system";
        case Role::Tool: return "tool";
    }
    return "user";
}

/// 字符串转 Role（高性能：避免字符串拷贝）
inline Role string_to_role(std::string_view str) noexcept {
    if (str == "assistant") return Role::Assistant;
    if (str == "system") return Role::System;
    if (str == "tool") return Role::Tool;
    return Role::User;
}

/// ContentType 转字符串
inline const char* content_type_to_string(ContentType type) noexcept {
    switch (type) {
        case ContentType::Text: return "text";
        case ContentType::Image: return "image";
        case ContentType::Audio: return "audio";
        case ContentType::Video: return "video";
        case ContentType::ToolUse: return "tool_use";
        case ContentType::ToolResult: return "tool_result";
        case ContentType::Thinking: return "thinking";
    }
    return "text";
}

/// 字符串转 ContentType
inline ContentType string_to_content_type(std::string_view str) noexcept {
    if (str == "image") return ContentType::Image;
    if (str == "audio") return ContentType::Audio;
    if (str == "video") return ContentType::Video;
    if (str == "tool_use") return ContentType::ToolUse;
    if (str == "tool_result") return ContentType::ToolResult;
    if (str == "thinking") return ContentType::Thinking;
    return ContentType::Text;
}

} // namespace ben_gear::acp
