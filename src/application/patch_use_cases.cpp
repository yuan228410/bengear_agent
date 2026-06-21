#include "ben_gear/application/patch_use_cases.hpp"

#include "ben_gear/patch/patch_service.hpp"

#include <string_view>

namespace ben_gear::application {

namespace {

using ben_gear::base::container::String;

domain::AppError json_error(const Json& json, const char* fallback_code, const char* fallback_message) {
    auto code = json.value("error_type", fallback_code);
    auto message = json.value("message", fallback_message);
    return domain::AppError::invalid_argument(String(code.c_str()), String(message.c_str()));
}

std::string json_string_at(const Json& json, std::string_view key) {
    if (!json.contains(std::string(key))) return {};
    const auto& value = json[std::string(key)];
    if (!value.is_string()) return {};
    return value.get<std::string>();
}

std::vector<patch::ChangedFileRecord> parse_changed_files(const Json& json) {
    std::vector<patch::ChangedFileRecord> files;
    if (!json.contains("files") || !json["files"].is_array()) return files;

    const auto& array = json["files"];
    for (size_t i = 0; i < array.size(); ++i) {
        const auto& item = array[i];
        if (!item.is_object()) continue;
        patch::ChangedFileRecord file;
        file.path = json_string_at(item, "path");
        file.kind = json_string_at(item, "kind");
        file.existed_before = item.value("existed_before", false);
        file.exists_after = item.value("exists_after", false);
        file.before_hash = json_string_at(item, "before_hash");
        file.after_hash = json_string_at(item, "after_hash");
        file.before_content = json_string_at(item, "before_content");
        files.push_back(std::move(file));
    }
    return files;
}

std::vector<std::string> parse_change_file_paths(const Json& change_json) {
    std::vector<std::string> paths;
    if (!change_json.contains("change") || !change_json["change"].is_object()) return paths;
    const auto& change = change_json["change"];
    if (!change.contains("files") || !change["files"].is_array()) return paths;
    const auto& files = change["files"];
    for (size_t i = 0; i < files.size(); ++i) {
        auto path = json_string_at(files[i], "path");
        if (!path.empty()) paths.push_back(std::move(path));
    }
    return paths;
}

PatchApplyResult parse_apply_result(const Json& json) {
    PatchApplyResult result;
    result.change_id = json_string_at(json, "change_id");
    result.files = parse_changed_files(json);
    if (json.contains("summary") && json["summary"].is_object()) {
        const auto& summary = json["summary"];
        result.files_changed = summary.value("files_changed", static_cast<int>(result.files.size()));
        result.additions = summary.value("additions", 0);
        result.deletions = summary.value("deletions", 0);
    } else {
        result.files_changed = static_cast<int>(result.files.size());
    }
    return result;
}

PatchRevertResult parse_revert_result(const Json& json) {
    PatchRevertResult result;
    result.change_id = json_string_at(json, "change_id");
    if (json.contains("reverted_files") && json["reverted_files"].is_array()) {
        const auto& files = json["reverted_files"];
        for (size_t i = 0; i < files.size(); ++i) {
            if (files[i].is_string()) result.reverted_files.push_back(files[i].get<std::string>());
        }
    }
    return result;
}

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
                container::String(preview.error_type.empty() ? "invalid_patch" : preview.error_type),
                container::String(preview.message.empty() ? "patch could not be parsed" : preview.message)));
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
                String(preview.error_type.empty() ? "invalid_patch" : preview.error_type),
                String(preview.message.empty() ? "patch could not be parsed" : preview.message)));
    }

    CommandDescriptor descriptor;
    descriptor.action = String("patch.apply");
    descriptor.username = resolved.value().request.username;
    descriptor.workspace_name = resolved.value().request.workspace_name;
    descriptor.session_id = resolved.value().request.session_id;
    descriptor.project_path = resolved.value().project_path;
    descriptor.risk = CommandRisk::workspace_write;
    descriptor.mutates_workspace = true;
    for (const auto& file : preview.files) {
        auto path = file.kind == patch::FileChangeKind::remove ? file.old_path : file.new_path;
        descriptor.affected_paths.push_back(String(path.generic_string().c_str()));
    }

    return command_pipeline_.execute<PatchApplyResult>(descriptor, [&]() {
        auto json = service.apply(command.unified_diff, command.description);
        if (!json.value("success", false)) {
            return domain::AppResult<PatchApplyResult>::failure(
                json_error(json, "patch_apply_failed", "patch apply failed"));
        }
        return domain::AppResult<PatchApplyResult>::success(parse_apply_result(json));
    });
}

domain::AppResult<PatchRevertResult> PatchUseCases::revert_patch(const PatchRevertCommand& command) const {
    auto resolved = workspace_resolver_.resolve(command.request);
    if (!resolved.ok()) return domain::AppResult<PatchRevertResult>::failure(resolved.error());

    patch::PatchService service(resolved.value().to_workspace_context());
    auto change = service.read_change(command.change_id);
    if (!change.value("success", false)) {
        return domain::AppResult<PatchRevertResult>::failure(
            json_error(change, "change_not_found", "change not found"));
    }

    CommandDescriptor descriptor;
    descriptor.action = String("patch.revert");
    descriptor.username = resolved.value().request.username;
    descriptor.workspace_name = resolved.value().request.workspace_name;
    descriptor.session_id = resolved.value().request.session_id;
    descriptor.project_path = resolved.value().project_path;
    descriptor.risk = CommandRisk::workspace_write;
    descriptor.mutates_workspace = true;
    for (const auto& path : parse_change_file_paths(change)) {
        descriptor.affected_paths.push_back(String(path.c_str()));
    }

    return command_pipeline_.execute<PatchRevertResult>(descriptor, [&]() {
        auto json = service.revert(command.change_id, command.force);
        if (!json.value("success", false)) {
            return domain::AppResult<PatchRevertResult>::failure(
                json_error(json, "patch_revert_failed", "patch revert failed"));
        }
        return domain::AppResult<PatchRevertResult>::success(parse_revert_result(json));
    });
}

} // namespace ben_gear::application
