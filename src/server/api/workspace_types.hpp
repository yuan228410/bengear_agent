#pragma once

#include "server/api/common.hpp"

#include <optional>
#include <string>

namespace ben_gear::server {

struct WorkspaceInfo {
    std::string name;
    std::string path;
};

using ListWorkspacesFn = std::function<std::vector<WorkspaceInfo>(const std::string& username)>;
using CreateWorkspaceFn = std::function<std::optional<WorkspaceInfo>(const std::string& name, const std::string& project_path, const std::string& username)>;
using DeleteWorkspaceFn = std::function<bool(const std::string& name, const std::string& username)>;

struct WorkspaceService {
    ListWorkspacesFn list_workspaces;
    CreateWorkspaceFn create_workspace;
    DeleteWorkspaceFn delete_workspace;
};

} // namespace ben_gear::server
