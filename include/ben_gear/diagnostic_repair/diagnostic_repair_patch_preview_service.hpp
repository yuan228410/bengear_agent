#pragma once

#include "ben_gear/base/utils/json.hpp"
#include "ben_gear/diagnostic_repair/diagnostic_repair_plan_service.hpp"
#include "ben_gear/patch/patch_service.hpp"
#include "ben_gear/workspace/types.hpp"

#include <memory>

namespace ben_gear::diagnostic_repair {

class DiagnosticRepairPatchPreviewService {
public:
    explicit DiagnosticRepairPatchPreviewService(
        workspace::WorkspaceContext ws_ctx,
        std::shared_ptr<DiagnosticRepairPlanService> plan_service = nullptr,
        std::shared_ptr<patch::PatchService> patch_service = nullptr);

    Json repair_patch_preview(const Json& request) const;

private:
    workspace::WorkspaceContext ws_ctx_;
    std::shared_ptr<DiagnosticRepairPlanService> plan_service_;
    std::shared_ptr<patch::PatchService> patch_service_;
};

} // namespace ben_gear::diagnostic_repair
