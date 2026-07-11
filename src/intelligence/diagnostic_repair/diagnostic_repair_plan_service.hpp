#pragma once

#include "intelligence/diagnostic_context/diagnostic_context_service.hpp"
#include "intelligence/diagnostic_repair/types.hpp"
#include "base/domain/result.hpp"
#include "workspace/types.hpp"
#include "intelligence/workspace_index/request_index_session.hpp"

#include <filesystem>
#include <memory>

namespace ben_gear::diagnostic_repair {

class DiagnosticRepairPlanService {
public:
    explicit DiagnosticRepairPlanService(
        workspace::WorkspaceContext ws_ctx,
        std::shared_ptr<diagnostic_context::DiagnosticContextService> context_service = nullptr);

    domain::AppResult<RepairPlanResult> repair_plan(RepairPlanRequest request) const;
    domain::AppResult<RepairPlanResult> repair_plan(RepairPlanRequest request,
                                                    workspace_index::RequestIndexSession& request_session) const;

private:
    std::filesystem::path project_root() const;

    workspace::WorkspaceContext ws_ctx_;
    std::shared_ptr<diagnostic_context::DiagnosticContextService> context_service_;
};

} // namespace ben_gear::diagnostic_repair
