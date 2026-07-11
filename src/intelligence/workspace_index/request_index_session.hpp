#pragma once

#include "intelligence/repo_map/types.hpp"
#include "intelligence/workspace_index/workspace_index_service.hpp"

#include <memory>
#include <string>
#include <unordered_map>

namespace ben_gear::workspace_index {

class RequestIndexSession {
public:
    explicit RequestIndexSession(std::shared_ptr<WorkspaceIndexService> service);

    repo_map::RepoMapIndex snapshot(const WorkspaceIndexOptions& options,
                                    const WorkspaceIndexService::BuildIndexFn& build_index);

private:
    std::shared_ptr<WorkspaceIndexService> service_;
    std::unordered_map<std::string, repo_map::RepoMapIndex> snapshots_;
};

} // namespace ben_gear::workspace_index
