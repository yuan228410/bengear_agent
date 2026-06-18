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
    std::string permission_id;
    Json resource = Json::object();

    bool allowed() const noexcept { return effect == PolicyEffect::allow; }
};

struct PermissionRequest {
    std::string permission_id;
    std::string policy_key;
    std::string tool_name;
    std::string reason;
    std::string created_at;
    Json arguments = Json::object();
    Json resource = Json::object();
};

std::string to_string(PolicyEffect effect);
Json to_json(const PermissionDecision& decision);
Json to_json(const PermissionRequest& request);

class ToolPermissionProvider {
public:
    virtual ~ToolPermissionProvider() = default;
    virtual PermissionDecision evaluate_tool_permission(std::string_view tool_name,
                                                        const Json& arguments) const = 0;
    virtual Json before_tool_execution(std::string_view, const Json&) const {
        return Json{{"success", true}, {"skipped", true}};
    }
};

} // namespace ben_gear::permission
