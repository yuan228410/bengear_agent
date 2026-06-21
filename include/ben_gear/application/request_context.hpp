#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/tier_paths.hpp"
#include "ben_gear/workspace/types.hpp"

#include <filesystem>

namespace ben_gear::application {

namespace container = base::container;

struct RequestContext {
    container::String request_id;
    container::String username;
    container::String workspace_name;
    container::String session_id;
};

struct ResolvedWorkspaceContext {
    RequestContext request;
    workspace::TierPaths tier_paths;
    std::filesystem::path user_dir;
    std::filesystem::path workspace_dir;
    container::String project_path;

    workspace::WorkspaceContext to_workspace_context() const {
        return workspace::WorkspaceContext{tier_paths,
                                           request.workspace_name,
                                           project_path,
                                           request.username,
                                           request.session_id};
    }
};

} // namespace ben_gear::application
