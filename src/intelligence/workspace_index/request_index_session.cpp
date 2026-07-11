#include "intelligence/workspace_index/request_index_session.hpp"

namespace ben_gear::workspace_index {

RequestIndexSession::RequestIndexSession(std::shared_ptr<WorkspaceIndexService> service)
    : service_(std::move(service)) {}

repo_map::RepoMapIndex RequestIndexSession::snapshot(const WorkspaceIndexOptions& options,
                                                     const WorkspaceIndexService::BuildIndexFn& build_index) {
    auto key = cache_key(options, {}, {});
    if (auto found = snapshots_.find(key); found != snapshots_.end()) return found->second;
    auto index = service_ ? service_->snapshot(options, build_index) : build_index();
    snapshots_[key] = index;
    return index;
}

} // namespace ben_gear::workspace_index
