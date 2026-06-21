#pragma once

#include "ben_gear/server/api/common.hpp"

#include <optional>
#include <string>

namespace ben_gear::server {

struct WorkspaceInfo {
    container::String name;
    std::string path;
};

using ListWorkspacesFn = std::function<container::Vector<WorkspaceInfo>(const container::String& username)>;
using CreateWorkspaceFn = std::function<std::optional<WorkspaceInfo>(const container::String& name, const container::String& project_path, const container::String& username)>;
using DeleteWorkspaceFn = std::function<bool(const container::String& name, const container::String& username)>;

struct WorkspaceService {
    ListWorkspacesFn list_workspaces;
    CreateWorkspaceFn create_workspace;
    DeleteWorkspaceFn delete_workspace;
};

} // namespace ben_gear::server
