#include "agent/runtime/application/workspace_resolver.hpp"

#include "workspace/manager.hpp"

namespace ben_gear::application {

WorkspaceResolver::WorkspaceResolver(WorkspaceResolverConfig config)
    : config_(std::move(config)) {
    if (config_.default_workspace.empty()) config_.default_workspace = std::string("default");
}

std::string WorkspaceResolver::workspace_or_default(const std::string& workspace) const {
    return workspace.empty() ? config_.default_workspace : workspace;
}

std::filesystem::path WorkspaceResolver::user_dir_for(const std::string& username) const {
    return config_.data_root / "users" / username;
}

workspace::TierPaths WorkspaceResolver::tier_paths_for(const std::string& username,
                                                       const std::string& workspace) const {
    auto user_dir = user_dir_for(username);
    auto ws = workspace_or_default(workspace);
    return workspace::TierPaths{config_.data_root,
                                user_dir,
                                user_dir / "workspaces" / ws};
}

std::string WorkspaceResolver::project_path_for(const std::string& username,
                                                      const std::string& workspace) const {
    workspace::WorkspaceManager manager(user_dir_for(username));
    auto ws = workspace_or_default(workspace);
    auto meta = manager.get(ws);
    if (meta && !meta->project_path.empty()) return meta->project_path;
    return config_.fallback_project_path;
}

domain::AppResult<ResolvedWorkspaceContext> WorkspaceResolver::resolve(const RequestContext& request) const {
    RequestContext normalized = request;
    normalized.workspace_name = workspace_or_default(request.workspace_name);

    ResolvedWorkspaceContext resolved;
    resolved.request = normalized;
    resolved.tier_paths = tier_paths_for(normalized.username, normalized.workspace_name);
    resolved.user_dir = resolved.tier_paths.user_dir;
    resolved.workspace_dir = resolved.tier_paths.workspace_dir;
    resolved.project_path = project_path_for(normalized.username, normalized.workspace_name);

    return domain::AppResult<ResolvedWorkspaceContext>::success(std::move(resolved));
}

} // namespace ben_gear::application
