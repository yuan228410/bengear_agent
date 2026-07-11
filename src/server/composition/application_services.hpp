#pragma once

#include "capabilities/checkpoint/checkpoint_service.hpp"
#include "intelligence/code_intel/code_intel_service.hpp"
#include "intelligence/code_intel/code_intelligence_index.hpp"
#include "intelligence/diagnostic_context/diagnostic_context_service.hpp"
#include "intelligence/diagnostic_repair/diagnostic_repair_plan_service.hpp"
#include "capabilities/git/git_service.hpp"
#include "capabilities/patch/patch_service.hpp"
#include "intelligence/repo_map/repo_map_service.hpp"
#include "capabilities/test_loop/test_loop_service.hpp"
#include "workspace/types.hpp"
#include "intelligence/workspace_index/workspace_index_service.hpp"

#include <memory>

namespace ben_gear::server::composition {

class WorkspaceApplicationServices {
public:
    explicit WorkspaceApplicationServices(workspace::WorkspaceContext ws_ctx);

    const workspace::WorkspaceContext& workspace_context() const { return ws_ctx_; }
    std::shared_ptr<workspace_index::WorkspaceIndexService> workspace_index();
    std::shared_ptr<git::GitService> git();
    std::shared_ptr<checkpoint::CheckpointService> checkpoint();
    std::shared_ptr<test_loop::TestLoopService> test_loop();
    std::shared_ptr<repo_map::RepoMapService> repo_map();
    std::shared_ptr<code_intel::CodeIntelService> code_intel();
    std::shared_ptr<code_intel::CodeIntelligenceIndex> code_intelligence_index();
    std::shared_ptr<diagnostic_context::DiagnosticContextService> diagnostic_context();
    std::shared_ptr<diagnostic_repair::DiagnosticRepairPlanService> diagnostic_repair_plan();
    std::shared_ptr<patch::PatchService> patch();

private:
    workspace::WorkspaceContext ws_ctx_;
    std::shared_ptr<workspace_index::WorkspaceIndexService> workspace_index_;
    std::shared_ptr<git::GitService> git_;
    std::shared_ptr<checkpoint::CheckpointService> checkpoint_;
    std::shared_ptr<test_loop::TestLoopService> test_loop_;
    std::shared_ptr<repo_map::RepoMapService> repo_map_;
    std::shared_ptr<code_intel::CodeIntelService> code_intel_;
    std::shared_ptr<code_intel::CodeIntelligenceIndex> code_intelligence_index_;
    std::shared_ptr<diagnostic_context::DiagnosticContextService> diagnostic_context_;
    std::shared_ptr<diagnostic_repair::DiagnosticRepairPlanService> diagnostic_repair_plan_;
    std::shared_ptr<patch::PatchService> patch_;
};

} // namespace ben_gear::server::composition
