#pragma once

#include "base/tier_paths.hpp"
#include "base/core/runtime_boundary.hpp"
#include "workspace/workspace_types.hpp"
#include "domain/result.hpp"

#include <filesystem>
#include <string>

namespace ben_gear::workspace {

struct WorkspaceResolverConfig {
    std::filesystem::path data_root;
    std::string default_workspace = std::string("default");
    std::string fallback_project_path;
};

struct ResolvedWorkspaceContext {
    base::core::RequestContext request;
    TierPaths tier_paths;
    std::filesystem::path user_dir;
    std::filesystem::path workspace_dir;
    std::string project_path;

    WorkspaceContext to_workspace_context() const {
        return WorkspaceContext{tier_paths,
                                request.workspace_name,
                                project_path,
                                request.username,
                                request.session_id};
    }
};

class WorkspaceResolver {
public:
    const std::filesystem::path& data_root() const { return config_.data_root; }
    explicit WorkspaceResolver(WorkspaceResolverConfig config);

    std::string workspace_or_default(const std::string& workspace) const;
    std::filesystem::path user_dir_for(const std::string& username) const;
    TierPaths tier_paths_for(const std::string& username,
                             const std::string& workspace) const;
    std::string project_path_for(const std::string& username,
                                       const std::string& workspace) const;

    domain::AppResult<ResolvedWorkspaceContext> resolve(const base::core::RequestContext& request) const;

private:
    WorkspaceResolverConfig config_;
};

} // namespace ben_gear::workspace
