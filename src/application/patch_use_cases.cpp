#include "ben_gear/application/patch_use_cases.hpp"

#include "ben_gear/patch/patch_service.hpp"

namespace ben_gear::application {

PatchUseCases::PatchUseCases(const WorkspaceResolver& workspace_resolver)
    : workspace_resolver_(workspace_resolver) {}

domain::AppResult<patch::PatchPreview> PatchUseCases::preview_patch(const PatchPreviewQuery& query) const {
    auto resolved = workspace_resolver_.resolve(query.request);
    if (!resolved.ok()) return domain::AppResult<patch::PatchPreview>::failure(resolved.error());

    patch::PatchService service(resolved.value().to_workspace_context());
    auto preview = service.preview(query.unified_diff);
    if (!preview.success) {
        return domain::AppResult<patch::PatchPreview>::failure(
            domain::AppError::invalid_argument(
                container::String(preview.error_type.empty() ? "invalid_patch" : preview.error_type),
                container::String(preview.message.empty() ? "patch could not be parsed" : preview.message)));
    }
    return domain::AppResult<patch::PatchPreview>::success(std::move(preview));
}

} // namespace ben_gear::application
