#include "server/composition/application_services.hpp"

#include <utility>

namespace ben_gear::server::composition {

WorkspaceApplicationServices::WorkspaceApplicationServices(workspace::WorkspaceContext ws_ctx)
    : ws_ctx_(std::move(ws_ctx)) {}

std::shared_ptr<workspace_index::WorkspaceIndexService> WorkspaceApplicationServices::workspace_index() {
    if (!workspace_index_) workspace_index_ = std::make_shared<workspace_index::WorkspaceIndexService>(ws_ctx_);
    return workspace_index_;
}

std::shared_ptr<repo_map::RepoMapService> WorkspaceApplicationServices::repo_map() {
    if (!repo_map_) repo_map_ = std::make_shared<repo_map::RepoMapService>(ws_ctx_, workspace_index());
    return repo_map_;
}

std::shared_ptr<code_intel::CodeIntelService> WorkspaceApplicationServices::code_intel() {
    if (!code_intel_) code_intel_ = std::make_shared<code_intel::CodeIntelService>(ws_ctx_, repo_map());
    return code_intel_;
}

std::shared_ptr<code_intel::CodeIntelligenceIndex> WorkspaceApplicationServices::code_intelligence_index() {
    if (!code_intelligence_index_) code_intelligence_index_ = std::make_shared<code_intel::CodeIntelligenceIndex>(ws_ctx_, repo_map(), code_intel());
    return code_intelligence_index_;
}

} // namespace ben_gear::server::composition
