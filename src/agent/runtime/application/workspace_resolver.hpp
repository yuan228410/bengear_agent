#pragma once

#include "agent/runtime/application/request_context.hpp"
#include "domain/result.hpp"

#include <filesystem>

namespace ben_gear::application {

struct WorkspaceResolverConfig {
    std::filesystem::path data_root;
    std::string default_workspace = std::string("default");
    std::string fallback_project_path;
};

class WorkspaceResolver {
public:
    const std::filesystem::path& data_root() const { return config_.data_root; }
    explicit WorkspaceResolver(WorkspaceResolverConfig config);

    std::string workspace_or_default(const std::string& workspace) const;
    std::filesystem::path user_dir_for(const std::string& username) const;
    workspace::TierPaths tier_paths_for(const std::string& username,
                                        const std::string& workspace) const;
    std::string project_path_for(const std::string& username,
                                       const std::string& workspace) const;

    domain::AppResult<ResolvedWorkspaceContext> resolve(const RequestContext& request) const;

private:
    WorkspaceResolverConfig config_;
};

} // namespace ben_gear::application
