#pragma once

#include "ben_gear/repo_map/types.hpp"
#include "ben_gear/workspace/types.hpp"
#include "ben_gear/workspace_index/types.hpp"

#include <functional>
#include <mutex>
#include <optional>
#include <string>

namespace ben_gear::workspace_index {

class WorkspaceIndexService {
public:
    using BuildIndexFn = std::function<repo_map::RepoMapIndex()>;

    explicit WorkspaceIndexService(workspace::WorkspaceContext ws_ctx);

    repo_map::RepoMapIndex snapshot(const WorkspaceIndexOptions& options, BuildIndexFn build_index);
    void invalidate();
    WorkspaceIndexMetrics metrics() const;

private:
    std::string project_root() const;
    std::string root_signature(const WorkspaceIndexOptions& options) const;

    workspace::WorkspaceContext ws_ctx_;
    mutable std::mutex mutex_;
    std::optional<repo_map::RepoMapIndex> cached_index_;
    std::string cached_key_;
    WorkspaceIndexMetrics metrics_;
};

} // namespace ben_gear::workspace_index
