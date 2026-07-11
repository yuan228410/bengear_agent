#pragma once

#include "intelligence/diagnostic_repair/diagnostic_repair_patch_preview_service.hpp"
#include "intelligence/diagnostic_repair/types.hpp"
#include "base/domain/result.hpp"
#include "workspace/types.hpp"

#include <memory>

namespace ben_gear::diagnostic_repair {

class DiagnosticRepairPatchDraftService {
public:
    explicit DiagnosticRepairPatchDraftService(
        workspace::WorkspaceContext ws_ctx,
        std::shared_ptr<DiagnosticRepairPatchPreviewService> preview_service = nullptr);

    domain::AppResult<RepairPatchDraftResult> repair_patch_draft(RepairPatchDraftRequest request) const;

private:
    workspace::WorkspaceContext ws_ctx_;
    std::shared_ptr<DiagnosticRepairPatchPreviewService> preview_service_;
};

} // namespace ben_gear::diagnostic_repair
