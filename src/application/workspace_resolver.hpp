#pragma once

#include "application/request_context.hpp"
#include "base/domain/result.hpp"

#include <filesystem>

namespace ben_gear::application {

struct WorkspaceResolverConfig {
    std::filesystem::path data_root;
    container::String default_workspace = container::String("default");
    container::String fallback_project_path;
};

class WorkspaceResolver {
public:
    explicit WorkspaceResolver(WorkspaceResolverConfig config);

    container::String workspace_or_default(const container::String& workspace) const;
    std::filesystem::path user_dir_for(const container::String& username) const;
    workspace::TierPaths tier_paths_for(const container::String& username,
                                        const container::String& workspace) const;
    container::String project_path_for(const container::String& username,
                                       const container::String& workspace) const;

    domain::AppResult<ResolvedWorkspaceContext> resolve(const RequestContext& request) const;

private:
    WorkspaceResolverConfig config_;
};

} // namespace ben_gear::application
