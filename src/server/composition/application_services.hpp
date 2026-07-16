#pragma once

#include "intelligence/code_intel/code_intel_service.hpp"
#include "intelligence/code_intel/code_intelligence_index.hpp"
#include "intelligence/repo_map/repo_map_service.hpp"
#include "intelligence/workspace_index/workspace_index_service.hpp"
#include "workspace/types.hpp"

#include <memory>

namespace ben_gear::server::composition {

class WorkspaceApplicationServices {
public:
    explicit WorkspaceApplicationServices(workspace::WorkspaceContext ws_ctx);

    const workspace::WorkspaceContext& workspace_context() const { return ws_ctx_; }
    std::shared_ptr<workspace_index::WorkspaceIndexService> workspace_index();
    std::shared_ptr<repo_map::RepoMapService> repo_map();
    std::shared_ptr<code_intel::CodeIntelService> code_intel();
    std::shared_ptr<code_intel::CodeIntelligenceIndex> code_intelligence_index();

private:
    workspace::WorkspaceContext ws_ctx_;
    std::shared_ptr<workspace_index::WorkspaceIndexService> workspace_index_;
    std::shared_ptr<repo_map::RepoMapService> repo_map_;
    std::shared_ptr<code_intel::CodeIntelService> code_intel_;
    std::shared_ptr<code_intel::CodeIntelligenceIndex> code_intelligence_index_;
};

} // namespace ben_gear::server::composition
