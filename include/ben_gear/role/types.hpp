#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/vector.hpp"

namespace ben_gear::role {

namespace container = base::container;

/// 角色定义
struct RoleDefinition {
    container::String name;                                 // 角色名
    container::String description;                          // LLM 行为描述
    container::Vector<container::String> tool_whitelist;    // 允许的工具名列表，空=全部
    container::String tier;                                 // 来源层级 "global"/"user"/"workspace"

    /// 是否允许指定工具
    bool is_tool_allowed(std::string_view tool_name) const {
        if (tool_whitelist.empty()) return true;  // 空白名单=不过滤
        for (const auto& allowed : tool_whitelist) {
            if (std::string_view(allowed.data(), allowed.size()) == tool_name) {
                return true;
            }
        }
        return false;
    }

    /// 是否不过滤（lead 角色）
    bool no_filter() const { return tool_whitelist.empty(); }
};

}  // namespace ben_gear::role
