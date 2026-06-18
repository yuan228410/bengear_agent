#pragma once

#include "ben_gear/permission/types.hpp"
#include "ben_gear/workspace/types.hpp"

#include <filesystem>
#include <set>
#include <string>

namespace ben_gear::permission {

class PolicyEngine : public ToolPermissionProvider {
public:
    explicit PolicyEngine(workspace::WorkspaceContext ws_ctx);

    PermissionDecision evaluate_tool_permission(std::string_view tool_name, const Json& arguments) const override;
    PermissionDecision evaluate_tool(std::string_view tool_name, const Json& arguments) const;
    void allow_for_session(std::string policy_key);

private:
    std::filesystem::path project_root() const;
    PermissionDecision allow(std::string key, std::string reason = {}) const;
    PermissionDecision ask(std::string key, std::string reason, Json resource = Json::object()) const;
    PermissionDecision deny(std::string key, std::string reason, Json resource = Json::object()) const;
    bool path_inside_workspace(const std::string& input, std::string& normalized) const;
    bool dangerous_shell_command(const std::string& command) const;

    workspace::WorkspaceContext ws_ctx_;
    std::set<std::string> session_allow_;
};

} // namespace ben_gear::permission
