#pragma once

#include "server/api/common.hpp"

#include <memory>
#include <optional>
#include <string>

namespace ben_gear::server {

struct WorkspaceInfo {
    std::string name;
    std::string path;
};

class WorkspaceService {
public:
    virtual ~WorkspaceService() = default;

    virtual std::vector<WorkspaceInfo> list_workspaces(const std::string& username) = 0;
    virtual std::optional<WorkspaceInfo> create_workspace(const std::string& name, const std::string& project_path, const std::string& username) = 0;
    virtual bool delete_workspace(const std::string& name, const std::string& username) = 0;
};

} // namespace ben_gear::server
