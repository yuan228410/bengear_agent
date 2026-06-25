#include "ben_gear/server/composition/application_services.hpp"

#include <utility>

namespace ben_gear::server::composition {

WorkspaceApplicationServices::WorkspaceApplicationServices(workspace::WorkspaceContext ws_ctx)
    : ws_ctx_(std::move(ws_ctx)) {}

std::shared_ptr<workspace_index::WorkspaceIndexService> WorkspaceApplicationServices::workspace_index() {
    if (!workspace_index_) workspace_index_ = std::make_shared<workspace_index::WorkspaceIndexService>(ws_ctx_);
    return workspace_index_;
}

std::shared_ptr<git::GitService> WorkspaceApplicationServices::git() {
    if (!git_) git_ = std::make_shared<git::GitService>(ws_ctx_);
    return git_;
}

std::shared_ptr<checkpoint::CheckpointService> WorkspaceApplicationServices::checkpoint() {
    if (!checkpoint_) checkpoint_ = std::make_shared<checkpoint::CheckpointService>(ws_ctx_);
    return checkpoint_;
}

std::shared_ptr<test_loop::TestLoopService> WorkspaceApplicationServices::test_loop() {
    if (!test_loop_) test_loop_ = std::make_shared<test_loop::TestLoopService>(ws_ctx_);
    return test_loop_;
}

std::shared_ptr<repo_map::RepoMapService> WorkspaceApplicationServices::repo_map() {
    if (!repo_map_) repo_map_ = std::make_shared<repo_map::RepoMapService>(ws_ctx_, git(), test_loop(), workspace_index());
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

std::shared_ptr<diagnostic_context::DiagnosticContextService> WorkspaceApplicationServices::diagnostic_context() {
    if (!diagnostic_context_) diagnostic_context_ = std::make_shared<diagnostic_context::DiagnosticContextService>(ws_ctx_, code_intel());
    return diagnostic_context_;
}

std::shared_ptr<diagnostic_repair::DiagnosticRepairPlanService> WorkspaceApplicationServices::diagnostic_repair_plan() {
    if (!diagnostic_repair_plan_) diagnostic_repair_plan_ = std::make_shared<diagnostic_repair::DiagnosticRepairPlanService>(ws_ctx_, diagnostic_context());
    return diagnostic_repair_plan_;
}

std::shared_ptr<patch::PatchService> WorkspaceApplicationServices::patch() {
    if (!patch_) patch_ = std::make_shared<patch::PatchService>(ws_ctx_);
    return patch_;
}

} // namespace ben_gear::server::composition
