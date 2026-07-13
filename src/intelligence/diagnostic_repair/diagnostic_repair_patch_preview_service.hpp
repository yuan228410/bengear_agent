#pragma once

#include "intelligence/diagnostic_repair/diagnostic_repair_plan_service.hpp"
#include "intelligence/diagnostic_repair/types.hpp"
#include "domain/result.hpp"
#include "capabilities/patch/patch_service.hpp"
#include "workspace/types.hpp"

#include <memory>

namespace ben_gear::diagnostic_repair {

class DiagnosticRepairPatchPreviewService {
public:
    explicit DiagnosticRepairPatchPreviewService(
        workspace::WorkspaceContext ws_ctx,
        std::shared_ptr<DiagnosticRepairPlanService> plan_service = nullptr,
        std::shared_ptr<patch::PatchService> patch_service = nullptr);

    domain::AppResult<RepairPatchPreviewResult> repair_patch_preview(RepairPatchPreviewRequest request) const;

private:
    workspace::WorkspaceContext ws_ctx_;
    std::shared_ptr<DiagnosticRepairPlanService> plan_service_;
    std::shared_ptr<patch::PatchService> patch_service_;
};

} // namespace ben_gear::diagnostic_repair
