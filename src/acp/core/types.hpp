#pragma once
#include <cstdint>
#include <string_view>
#include <string>

namespace ben_gear::acp {

// ==================== 协议版本 ====================

/// ACP 协议版本
struct ProtocolVersion {
    int major = 1;
    int minor = 0;

    /// 当前协议版本
    static constexpr ProtocolVersion current() noexcept { return {1, 0}; }

    /// 检查是否兼容（主版本号相同）
    bool is_compatible(const ProtocolVersion& other) const noexcept {
        return major == other.major;
    }

    /// 转换为字符串
    std::string to_string() const {
        return std::to_string(major) + "." + std::to_string(minor);
    }

    /// 从字符串解析
    static ProtocolVersion from_string(std::string_view str) {
        ProtocolVersion v;
        auto dot = str.find('.');
        if (dot != std::string_view::npos) {
            auto major_str = str.substr(0, dot);
            auto minor_str = str.substr(dot + 1);
            v.major = 0;
            for (char c : major_str) {
                if (c >= '0' && c <= '9') v.major = v.major * 10 + (c - '0');
            }
            v.minor = 0;
            for (char c : minor_str) {
                if (c >= '0' && c <= '9') v.minor = v.minor * 10 + (c - '0');
            }
        }
        return v;
    }
};

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
