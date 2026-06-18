#pragma once

#include "ben_gear/permission/types.hpp"
#include "ben_gear/workspace/types.hpp"

#include <filesystem>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace ben_gear::permission {

class PolicyEngine : public ToolPermissionProvider {
public:
    explicit PolicyEngine(workspace::WorkspaceContext ws_ctx);

    PermissionDecision evaluate_tool_permission(std::string_view tool_name, const Json& arguments) const override;
    PermissionDecision evaluate_tool(std::string_view tool_name, const Json& arguments) const;
    void allow_for_session(std::string policy_key);
    Json list_pending() const;
    Json approve(std::string_view permission_id, bool allow_session = false);
    Json deny_pending(std::string_view permission_id);

private:
    std::filesystem::path project_root() const;
    PermissionDecision allow(std::string key, std::string reason = {}) const;
    PermissionDecision ask(std::string key, std::string reason, std::string tool_name = {}, Json arguments = Json::object(), Json resource = Json::object()) const;
    PermissionDecision deny(std::string key, std::string reason, Json resource = Json::object()) const;
    std::string make_permission_id() const;
    std::string permission_fingerprint(std::string_view tool_name, const Json& arguments) const;
    bool path_inside_workspace(const std::string& input, std::string& normalized) const;
    bool dangerous_shell_command(const std::string& command) const;

    workspace::WorkspaceContext ws_ctx_;
    mutable std::mutex mutex_;
    std::set<std::string> session_allow_;
    mutable std::set<std::string> approved_once_;
    mutable std::map<std::string, PermissionRequest> pending_;
};

} // namespace ben_gear::permission
