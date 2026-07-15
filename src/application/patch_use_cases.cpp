#include "application/patch_use_cases.hpp"

#include "application/command_descriptor_factory.hpp"
#include "capabilities/patch/patch_service.hpp"

#include <string_view>

namespace ben_gear::application {

namespace {


} // namespace

PatchUseCases::PatchUseCases(const WorkspaceResolver& workspace_resolver,
                             CommandPipeline command_pipeline)
    : workspace_resolver_(workspace_resolver), command_pipeline_(std::move(command_pipeline)) {}

domain::AppResult<patch::PatchPreview> PatchUseCases::preview_patch(const PatchPreviewQuery& query) const {
    auto resolved = workspace_resolver_.resolve(query.request);
    if (!resolved.ok()) return domain::AppResult<patch::PatchPreview>::failure(resolved.error());

    patch::PatchService service(resolved.value().to_workspace_context());
    auto preview = service.preview(query.unified_diff);
    if (!preview.success) {
        return domain::AppResult<patch::PatchPreview>::failure(
            domain::AppError::invalid_argument(
                std::string(preview.error_type.empty() ? "invalid_patch" : preview.error_type),
                std::string(preview.message.empty() ? "patch could not be parsed" : preview.message)));
    }
    return domain::AppResult<patch::PatchPreview>::success(std::move(preview));
}

domain::AppResult<PatchApplyResult> PatchUseCases::apply_patch(const PatchApplyCommand& command) const {
    auto resolved = workspace_resolver_.resolve(command.request);
    if (!resolved.ok()) return domain::AppResult<PatchApplyResult>::failure(resolved.error());

    patch::PatchService service(resolved.value().to_workspace_context());
    auto preview = service.preview(command.unified_diff);
    if (!preview.success) {
        return domain::AppResult<PatchApplyResult>::failure(
            domain::AppError::invalid_argument(
                std::string(preview.error_type.empty() ? "invalid_patch" : preview.error_type),
                std::string(preview.message.empty() ? "patch could not be parsed" : preview.message)));
    }

    std::vector<std::string> affected_paths;
    for (const auto& file : preview.files) {
        auto path = file.kind == patch::FileChangeKind::remove ? file.old_path : file.new_path;
        affected_paths.push_back(path.generic_string());
    }
    auto descriptor = CommandDescriptorFactory(resolved.value().request, resolved.value().project_path)
                          .patch_apply(affected_paths);

    return command_pipeline_.execute<PatchApplyResult>(descriptor, [&]() {
        return service.apply(command.unified_diff, command.description);
    });
}

domain::AppResult<PatchRevertResult> PatchUseCases::revert_patch(const PatchRevertCommand& command) const {
    auto resolved = workspace_resolver_.resolve(command.request);
    if (!resolved.ok()) return domain::AppResult<PatchRevertResult>::failure(resolved.error());

    patch::PatchService service(resolved.value().to_workspace_context());
    auto change = service.read_change(command.change_id);
    if (!change.ok()) return domain::AppResult<PatchRevertResult>::failure(change.error());

    std::vector<std::string> affected_paths;
    for (const auto& file : change.value().change.files) {
        affected_paths.push_back(file.path);
    }
    auto descriptor = CommandDescriptorFactory(resolved.value().request, resolved.value().project_path)
                          .patch_revert(command.change_id, affected_paths, command.force);

    return command_pipeline_.execute<PatchRevertResult>(descriptor, [&]() {
        return service.revert(command.change_id, command.force);
    });
}

} // namespace ben_gear::application
