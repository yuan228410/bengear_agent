#pragma once

#include "base/container/string.hpp"
#include "base/tier_paths.hpp"
#include "base/core/runtime_boundary.hpp"
#include "workspace/types.hpp"

#include <filesystem>

namespace ben_gear::application {

namespace container = base::container;

using RequestContext = core::RequestContext;

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
