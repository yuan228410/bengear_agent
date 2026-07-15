#include "application/safe_code_change_service.hpp"

#include "application/command_descriptor_factory.hpp"
#include "application/command_governance.hpp"
#include "capabilities/checkpoint/checkpoint_service.hpp"
#include "capabilities/git/git_service.hpp"
#include "capabilities/patch/patch_service.hpp"
#include "capabilities/test_loop/test_loop_service.hpp"

#include <string>
#include <utility>

namespace ben_gear::application {

namespace {


std::string make_string(std::string_view value) {
    return std::string(value);
}

std::vector<std::string> affected_paths_from_preview(const patch::PatchPreview& preview) {
    std::vector<std::string> paths;
    paths.reserve(preview.files.size());
    for (const auto& file : preview.files) {
        auto path = file.kind == patch::FileChangeKind::remove ? file.old_path : file.new_path;
        paths.push_back(path.generic_string());
    }
    return paths;
}

domain::AppError pipeline_error(std::string_view stage, const domain::AppError& source) {
    auto error = source;
    Json details{{"stage", std::string(stage)}};
    if (!source.details_json.empty()) details["source_details"] = source.details_json;
    error.details_json = make_string(details.dump());
    return error;
}

domain::AppError service_error(std::string_view stage,
                               std::string_view code,
                               std::string_view message,
                               Json details = Json::object()) {
    details["stage"] = std::string(stage);
    auto error = domain::AppError::internal(make_string(code), make_string(message));
    if (code == "invalid_patch" || code == "patch_parse_failed" || code == "no_patch_changes") {
        error = domain::AppError::invalid_argument(make_string(code), make_string(message));
    } else if (code == "patch_conflict") {
        error = domain::AppError::conflict(make_string(code), make_string(message));
    } else if (code == "test_failed") {
        error = domain::AppError::conflict(make_string(code), make_string(message));
    }
    error.details_json = make_string(details.dump());
    return error;
}

SafeCodeChangeResult failed_result(std::string_view stage, const domain::AppError& error) {
    SafeCodeChangeResult result;
    result.success = false;
    result.stage = std::string(stage);
    result.error_type = error.code;
    result.message = error.message;
    result.rollback_hint = "Inspect the checkpoint and use checkpoint.restore or patch.revert after reviewing diagnostics.";
    return result;
}

Json checkpoint_create_json(const checkpoint::CheckpointCreateResult& result) {
    if (result.checkpoint_id.empty()) return Json::object();
    Json json = checkpoint::to_json(result);
    if (json.contains("checkpoint") && json["checkpoint"].is_object() && json["checkpoint"].contains("files")) {
        Json files = Json::array();
        for (auto file : json["checkpoint"]["files"]) {
            if (file.is_object()) file.erase("content");
            files.push_back(std::move(file));
        }
        json["checkpoint"]["files"] = std::move(files);
    }
    return json;
}

} // namespace

Json to_json(const SafeCodeChangeResult& result) {
    Json json{{"success", result.success},
              {"stage", result.stage},
              {"error_type", result.error_type},
              {"message", result.message},
              {"preview", patch::to_json(result.preview)},
              {"checkpoint", checkpoint_create_json(result.checkpoint)},
              {"patch_apply", patch::to_json(result.patch_apply)},
              {"git_status", git::to_json(result.git_status)},
              {"git_diff", git::to_json(result.git_diff)},
              {"test_run", test_loop::to_json(result.test_run)},
              {"rollback_hint", result.rollback_hint},
              {"execution", result.execution}};
    return json;
}

SafeCodeChangeService::SafeCodeChangeService(const WorkspaceResolver& workspace_resolver,
                                             CommandPipeline command_pipeline,
                                             core::RuntimeEventSink event_sink,
                                             std::shared_ptr<code_intel::CodeIntelligenceIndex> code_intelligence)
    : workspace_resolver_(workspace_resolver),
      command_pipeline_(std::move(command_pipeline)),
      event_sink_(std::move(event_sink)),
      code_intelligence_(std::move(code_intelligence)) {}

domain::AppResult<SafeCodeChangeResult> SafeCodeChangeService::run(const SafeCodeChangeCommand& command) const {
    auto resolved = workspace_resolver_.resolve(command.request);
    if (!resolved.ok()) return domain::AppResult<SafeCodeChangeResult>::failure(resolved.error());

    auto ws_ctx = resolved.value().to_workspace_context();
    patch::PatchService patch_service(ws_ctx);
    checkpoint::CheckpointService checkpoint_service(ws_ctx);
    git::GitService git_service(ws_ctx);
    test_loop::TestLoopService test_service(ws_ctx);

    SafeCodeChangeResult result;
    result.stage = "preview";
    result.preview = patch_service.preview(command.unified_diff);
    if (!result.preview.success) {
        return domain::AppResult<SafeCodeChangeResult>::failure(
            service_error("preview",
                          result.preview.error_type.empty() ? "invalid_patch" : result.preview.error_type,
                          result.preview.message.empty() ? "patch could not be parsed" : result.preview.message,
                          Json{{"preview", patch::to_json(result.preview)}}));
    }
    auto affected_paths = affected_paths_from_preview(result.preview);
    if (affected_paths.empty()) {
        return domain::AppResult<SafeCodeChangeResult>::failure(
            service_error("preview", "no_patch_changes", "patch does not contain file changes"));
    }

    // Fill repo intelligence before applying changes
    if (code_intelligence_) {
        try {
            Json repo_intel = Json{{"success", true}};
            repo_intel["affected_paths"] = Json::array();
            for (const auto& path : affected_paths) {
                repo_intel["affected_paths"].push_back(path);
            }

            // Collect symbols and impacts from each affected file
            repo_intel["symbols"] = Json::array();
            repo_intel["impacts"] = Json::array();
            repo_intel["related_tests"] = Json::array();
            repo_intel["test_suggestions"] = Json::array();

            repo_map::RepoMapService::Options repo_options;
            repo_options.max_files = 100;
            repo_options.max_symbols = 200;

            for (const auto& path : affected_paths) {
                // Document symbols
                auto doc_symbols = code_intelligence_->document_symbols(path, code_intel::CodeIntelOptions{});
                if (doc_symbols.ok() && !doc_symbols.value().symbols.empty()) {
                    for (const auto& sym : doc_symbols.value().symbols) {
                        repo_intel["symbols"].push_back(code_intel::location_json(sym));
                    }
                }

                // Explain path to get dependencies and related tests
                auto explained = code_intelligence_->explain_path(path, repo_options);
                if (explained.ok()) {
                    Json impact;
                    impact["path"] = path;
                    impact["symbol_count"] = explained.value().symbols.size();
                    impact["dependent_count"] = explained.value().dependents.size();
                    impact["dependency_count"] = explained.value().dependencies.size();
                    impact["related_test_count"] = explained.value().related_tests.size();
                    repo_intel["impacts"].push_back(impact);

                    // Add related tests
                    for (const auto& test_file : explained.value().related_tests) {
                        Json test_json;
                        test_json["path"] = test_file.path;
                        test_json["kind"] = repo_map::to_string(test_file.kind);
                        test_json["language"] = test_file.language;
                        repo_intel["related_tests"].push_back(test_json);
                    }
                }
            }

            // Get test suggestions from overview
            auto overview = code_intelligence_->overview(repo_options);
            if (overview.ok()) {
                auto overview_json = repo_map::to_json(overview.value());
                if (overview_json.contains("summary") && overview_json["summary"].contains("test_suggestions")) {
                    repo_intel["test_suggestions"] = overview_json["summary"]["test_suggestions"];
                }
            }

            result.repo_intelligence = repo_intel;
        } catch (const std::exception& e) {
            result.repo_intelligence = Json{{"success", false}, {"error", e.what()}};
        }
    }

    auto descriptor = CommandDescriptorFactory(resolved.value().request, resolved.value().project_path)
                          .patch_apply(affected_paths);
    descriptor.action = std::string("safe_code_change.run");
    descriptor.subject = make_string(command.description.empty() ? "safe code change" : command.description);
    descriptor.risk = CommandRisk::command_execution;
    descriptor.runs_command = !command.test_command.empty();
    descriptor.working_directory = make_string(command.test_command);
    descriptor.timeout_seconds = command.test_timeout_seconds;
    descriptor.max_output_bytes = command.test_max_output_bytes;

    auto execution = command_pipeline_.execute<Json>(descriptor, [&]() -> domain::AppResult<Json> {
        result.stage = "checkpoint";
        auto checkpoint_result = checkpoint_service.create(affected_paths, "safe code change before apply: " + command.description);
        if (!checkpoint_result.ok()) return domain::AppResult<Json>::failure(pipeline_error("checkpoint", checkpoint_result.error()));
        result.checkpoint = std::move(checkpoint_result.value());

        result.stage = "apply_patch";
        auto apply_result = patch_service.apply(command.unified_diff, command.description);
        if (!apply_result.ok()) return domain::AppResult<Json>::failure(pipeline_error("apply_patch", apply_result.error()));
        result.patch_apply = std::move(apply_result.value());

        result.stage = "git_summary";
        result.git_status = git_service.status();
        auto diff_result = git_service.diff({}, false, true);
        if (diff_result.ok()) {
            result.git_diff = std::move(diff_result.value());
        } else {
            result.git_diff.diff = diff_result.error().message.c_str();
            result.git_diff.stat = true;
        }

        result.stage = "test_loop";
        result.test_run = test_loop::TestRunResult{};
        if (!command.test_command.empty()) {
            auto test_result = test_service.run(command.test_command,
                                                command.test_cwd,
                                                command.test_timeout_seconds,
                                                command.test_max_output_bytes);
            if (!test_result.ok()) return domain::AppResult<Json>::failure(pipeline_error("test_loop", test_result.error()));
            result.test_run = std::move(test_result.value());
            if (!result.test_run.success) {
                Json details{{"test_run", test_loop::to_json(result.test_run)},
                             {"checkpoint_id", result.checkpoint.checkpoint_id},
                             {"change_id", result.patch_apply.change_id}};
                return domain::AppResult<Json>::failure(
                    service_error("test_loop", "test_failed", "test command failed", std::move(details)));
            }
        }

        result.success = true;
        result.stage = "completed";
        result.rollback_hint.clear();
        return domain::AppResult<Json>::success(to_json(result));
    });

    if (!execution.ok()) {
        auto failed = failed_result(result.stage.empty() ? "execute" : result.stage, execution.error());
        failed.preview = std::move(result.preview);
        failed.checkpoint = std::move(result.checkpoint);
        failed.patch_apply = std::move(result.patch_apply);
        failed.git_status = std::move(result.git_status);
        failed.git_diff = std::move(result.git_diff);
        failed.test_run = std::move(result.test_run);
        failed.execution = Json{{"success", false},
                                {"error_type", execution.error().code},
                                {"message", execution.error().message},
                                {"details", execution.error().details_json}};
        return domain::AppResult<SafeCodeChangeResult>::failure(execution.error());
    }

    result.execution = execution.value();
    return domain::AppResult<SafeCodeChangeResult>::success(std::move(result));
}

} // namespace ben_gear::application
