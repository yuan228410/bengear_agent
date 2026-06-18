#pragma once

#include "ben_gear/base/utils/json.hpp"

#include <string>
#include <string_view>

namespace ben_gear::permission {

enum class PolicyEffect {
    allow,
    ask,
    deny,
};

struct PermissionDecision {
    PolicyEffect effect = PolicyEffect::allow;
    std::string policy_key;
    std::string reason;
    Json resource = Json::object();

    bool allowed() const noexcept { return effect == PolicyEffect::allow; }
};

std::string to_string(PolicyEffect effect);
Json to_json(const PermissionDecision& decision);

class ToolPermissionProvider {
public:
    virtual ~ToolPermissionProvider() = default;
    virtual PermissionDecision evaluate_tool_permission(std::string_view tool_name,
                                                        const Json& arguments) const = 0;
};

} // namespace ben_gear::permission
