#include "intelligence/diagnostic_repair/diagnostic_repair_workflow_service.hpp"

#include "application/command_descriptor_factory.hpp"
#include "capabilities/checkpoint/checkpoint_service.hpp"
#include "intelligence/diagnostic_repair/diagnostic_repair_patch_preview_service.hpp"
#include "intelligence/diagnostic_repair/diagnostic_repair_plan_service.hpp"
#include "capabilities/patch/patch_service.hpp"
#include "capabilities/test_loop/test_loop_service.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace ben_gear::diagnostic_repair {

namespace {



int clamp_iterations(int value) {
    return std::clamp(value, 1, 5);
}

Json safety_json() {
    return Json{{"requires_user_approval_before_edit", true},
                {"uses_patch_preview", true},
                {"uses_command_governance", true},
                {"uses_checkpoint_before_apply", true},
                {"uses_governed_restore_on_failure", true},
                {"max_iterations_enforced", true}};
}

Json error_json(const domain::AppError& error) {
    if (!error.details_json.empty()) {
        try {
            auto details = Json::parse(error.details_json);
            if (details.is_object()) return details;
        } catch (...) {
        }
    }
    return Json{{"success", false},
                {"error_type", error.code},
                {"message", error.message}};
}

Json patch_apply_json(const patch::PatchApplyResult& result) {
    return patch::to_json(result);
}

Json test_run_json(const test_loop::TestRunResult& result) {
    return test_loop::to_json(result);
}

Json checkpoint_create_json(const checkpoint::CheckpointCreateResult& result) {
    return checkpoint::to_json(result);
}

Json checkpoint_restore_json(const checkpoint::CheckpointRestoreResult& result) {
    return checkpoint::to_json(result);
}

std::string checkpoint_description(const RepairWorkflowRequest& request, const RepairWorkflowPatchCandidate& candidate) {
    if (!request.checkpoint_label.empty()) return request.checkpoint_label;
    if (!candidate.id.empty()) return "diagnostic repair checkpoint before " + candidate.id;
    return "diagnostic repair checkpoint before patch candidate";
}

std::vector<std::string> preview_paths(const patch::PatchPreview& preview) {
    std::vector<std::string> paths;
    for (const auto& file : preview.files) {
        auto path = file.kind == patch::FileChangeKind::remove ? file.old_path : file.new_path;
        paths.push_back(path.generic_string());
    }
    return paths;
}

} // namespace

DiagnosticRepairWorkflowService::DiagnosticRepairWorkflowService(
    const application::WorkspaceResolver& workspace_resolver,
    application::CommandPipeline command_pipeline)
    : workspace_resolver_(workspace_resolver), command_pipeline_(std::move(command_pipeline)) {}

domain::AppResult<RepairWorkflowRequest> repair_workflow_request_from_json(const Json& request) {
    auto plan_request = repair_plan_request_from_json(request);
    if (!plan_request.ok()) return domain::AppResult<RepairWorkflowRequest>::failure(plan_request.error());

    RepairWorkflowRequest parsed;
    parsed.plan_request = std::move(plan_request.value());
    parsed.request.username = std::string(request.value("username", ""));
    parsed.request.workspace_name = std::string(request.value("workspace", ""));
    parsed.request.session_id = std::string(request.value("session_id", ""));
    parsed.max_iterations = request.value("max_iterations", 1);
    parsed.apply_patch = request.value("apply_patch", true);
    parsed.rerun_tests = request.value("rerun_tests", true);
    parsed.checkpoint_before_apply = request.value("checkpoint_before_apply", true);
    parsed.restore_on_failure = request.value("restore_on_failure", true);
    parsed.checkpoint_label = request.value("checkpoint_label", "");

    if (request.contains("patch_candidates") && request["patch_candidates"].is_array()) {
        for (const auto& item : request["patch_candidates"]) {
            if (!item.is_object()) continue;
            RepairWorkflowPatchCandidate candidate;
            candidate.id = item.value("id", "");
            candidate.unified_diff = item.value("unified_diff", "");
            candidate.description = item.value("description", "diagnostic repair candidate");
            if (!candidate.unified_diff.empty()) parsed.patch_candidates.push_back(std::move(candidate));
        }
    } else if (request.contains("unified_diff") && request["unified_diff"].is_string()) {
        RepairWorkflowPatchCandidate candidate;
        candidate.id = request.value("plan_id", "candidate-1");
        candidate.unified_diff = request.value("unified_diff", "");
        candidate.description = request.value("description", "diagnostic repair candidate");
        if (!candidate.unified_diff.empty()) parsed.patch_candidates.push_back(std::move(candidate));
    }

    return domain::AppResult<RepairWorkflowRequest>::success(std::move(parsed));
}

domain::AppResult<RepairWorkflowResult> DiagnosticRepairWorkflowService::repair_workflow(const RepairWorkflowRequest& request) const {
    auto resolved = workspace_resolver_.resolve(request.request);
    if (!resolved.ok()) return domain::AppResult<RepairWorkflowResult>::failure(resolved.error());

    auto ws_ctx = resolved.value().to_workspace_context();
    DiagnosticRepairPlanService plan_service(ws_ctx);
    auto plan_result = plan_service.repair_plan(request.plan_request);
    if (!plan_result.ok()) return domain::AppResult<RepairWorkflowResult>::failure(plan_result.error());

    RepairWorkflowResult result;
    result.repair_plan = to_json(plan_result.value());
    result.safety = safety_json();

    auto max_iterations = clamp_iterations(request.max_iterations);
    auto iterations = std::min<int>(max_iterations, static_cast<int>(request.patch_candidates.size()));
    if (iterations == 0) {
        result.status = "no_patch_candidates";
        result.summary = Json{{"message", "repair plan generated but no patch candidates were provided"},
                              {"needs_patch_candidate", true}};
        return domain::AppResult<RepairWorkflowResult>::success(std::move(result));
    }

    DiagnosticRepairPatchPreviewService patch_preview_service(ws_ctx);
    patch::PatchService patch_service(ws_ctx);
    test_loop::TestLoopService test_service(ws_ctx);
    checkpoint::CheckpointService checkpoint_service(ws_ctx);
    auto factory = application::CommandDescriptorFactory(resolved.value().request, resolved.value().project_path);

    for (int i = 0; i < iterations; ++i) {
        const auto& candidate = request.patch_candidates[static_cast<size_t>(i)];
        Json attempt{{"iteration", i + 1}, {"candidate_id", candidate.id.empty() ? "candidate-" + std::to_string(i + 1) : candidate.id}};

        RepairPatchPreviewRequest preview_request;
        preview_request.plan_request = request.plan_request;
        preview_request.unified_diff = candidate.unified_diff;
        preview_request.plan_id = candidate.id;
        auto preview_result = patch_preview_service.repair_patch_preview(preview_request);
        if (!preview_result.ok()) {
            attempt["status"] = "preview_failed";
            attempt["error"] = error_json(preview_result.error());
            result.attempts.push_back(std::move(attempt));
            continue;
        }
        auto preview_json = to_json(preview_result.value());
        attempt["patch_preview"] = preview_json;
        bool safe_preview = preview_json.value("success", false) &&
                            preview_json.contains("patch_preview") &&
                            preview_json["patch_preview"].is_object() &&
                            preview_json["patch_preview"].value("can_apply", false);
        if (!safe_preview) {
            attempt["status"] = "unsafe_patch";
            result.attempts.push_back(std::move(attempt));
            continue;
        }

        auto raw_preview = patch_service.preview(candidate.unified_diff);
        if (!raw_preview.success || !raw_preview.can_apply) {
            attempt["status"] = "patch_not_applicable";
            attempt["raw_patch_preview"] = patch::to_json(raw_preview);
            result.attempts.push_back(std::move(attempt));
            continue;
        }

        auto candidate_paths = preview_paths(raw_preview);
        std::string checkpoint_id;
        if (request.apply_patch && request.checkpoint_before_apply) {
            auto checkpoint_result = checkpoint_service.create(candidate_paths, checkpoint_description(request, candidate));
            if (!checkpoint_result.ok()) {
                attempt["status"] = "checkpoint_failed";
                attempt["error"] = error_json(checkpoint_result.error());
                result.status = "checkpoint_failed";
                result.summary = Json{{"message", "failed to create checkpoint before applying repair candidate"},
                                      {"candidate_id", attempt.value("candidate_id", "")}};
                result.final_workspace_state = "unchanged_before_apply";
                result.attempts.push_back(std::move(attempt));
                return domain::AppResult<RepairWorkflowResult>::success(std::move(result));
            }
            checkpoint_id = checkpoint_result.value().checkpoint_id;
            attempt["checkpoint"] = checkpoint_create_json(checkpoint_result.value());
            result.checkpoint_id = checkpoint_id;
        }

        if (request.apply_patch) {
            auto apply_descriptor = factory.patch_apply(candidate_paths);
            auto apply_result = command_pipeline_.execute<patch::PatchApplyResult>(apply_descriptor, [&]() {
                return patch_service.apply(candidate.unified_diff, candidate.description);
            });
            if (!apply_result.ok()) {
                attempt["status"] = "apply_failed";
                attempt["error"] = error_json(apply_result.error());
                result.attempts.push_back(std::move(attempt));
                continue;
            }
            attempt["patch_apply"] = patch_apply_json(apply_result.value());
        } else {
            attempt["patch_apply"] = Json{{"skipped", true}};
        }

        if (request.rerun_tests && !request.plan_request.command.empty()) {
            auto test_descriptor = factory.test_run(request.plan_request.command,
                                                    request.plan_request.context.cwd,
                                                    request.plan_request.timeout_seconds,
                                                    request.plan_request.max_output_bytes);
            auto test_result = command_pipeline_.execute<test_loop::TestRunResult>(test_descriptor, [&]() {
                return test_service.run(request.plan_request.command,
                                        request.plan_request.context.cwd,
                                        request.plan_request.timeout_seconds,
                                        request.plan_request.max_output_bytes);
            });
            if (!test_result.ok()) {
                attempt["status"] = "rerun_failed";
                attempt["error"] = error_json(test_result.error());
                if (request.apply_patch && request.restore_on_failure && !checkpoint_id.empty()) {
                    auto restore_descriptor = factory.checkpoint_restore(checkpoint_id, candidate_paths, true);
                    auto restore_result = command_pipeline_.execute<checkpoint::CheckpointRestoreResult>(restore_descriptor, [&]() {
                        return checkpoint_service.restore(checkpoint_id, candidate_paths, true);
                    });
                    if (restore_result.ok()) {
                        attempt["restore"] = checkpoint_restore_json(restore_result.value());
                        result.restored = true;
                        result.restore_reason = "rerun_failed";
                        result.final_workspace_state = "restored";
                    } else {
                        attempt["restore_error"] = error_json(restore_result.error());
                        result.restore_reason = "restore_failed_after_rerun_error";
                        result.final_workspace_state = "possibly_dirty";
                    }
                }
                result.attempts.push_back(std::move(attempt));
                continue;
            }
            attempt["test_result"] = test_run_json(test_result.value());
            result.final_test_result = attempt["test_result"];
            result.iterations = i + 1;
            if (test_result.value().success) {
                attempt["status"] = "repaired";
                result.attempts.push_back(std::move(attempt));
                result.success = true;
                result.status = "repaired";
                result.final_workspace_state = request.apply_patch ? "patched" : "unchanged";
                result.summary = Json{{"message", "patch candidate applied and recommended tests passed"},
                                      {"iterations", result.iterations},
                                      {"checkpoint_id", checkpoint_id}};
                return domain::AppResult<RepairWorkflowResult>::success(std::move(result));
            }
            attempt["status"] = "rerun_failed_tests";
            if (request.apply_patch && request.restore_on_failure && !checkpoint_id.empty()) {
                auto restore_descriptor = factory.checkpoint_restore(checkpoint_id, candidate_paths, true);
                auto restore_result = command_pipeline_.execute<checkpoint::CheckpointRestoreResult>(restore_descriptor, [&]() {
                    return checkpoint_service.restore(checkpoint_id, candidate_paths, true);
                });
                if (restore_result.ok()) {
                    attempt["restore"] = checkpoint_restore_json(restore_result.value());
                    result.restored = true;
                    result.restore_reason = "rerun_failed_tests";
                    result.final_workspace_state = "restored";
                } else {
                    attempt["restore_error"] = error_json(restore_result.error());
                    result.restore_reason = "restore_failed_after_test_failure";
                    result.final_workspace_state = "possibly_dirty";
                }
            } else if (request.apply_patch && !request.restore_on_failure) {
                result.final_workspace_state = "patched_failed_tests";
            }
            result.attempts.push_back(std::move(attempt));
            continue;
        }

        attempt["status"] = request.rerun_tests ? "applied_without_rerun_command" : "applied";
        result.iterations = i + 1;
        result.attempts.push_back(std::move(attempt));
        result.success = !request.rerun_tests;
        result.status = request.rerun_tests ? "applied_without_rerun_command" : "applied";
        result.final_workspace_state = request.apply_patch ? "patched_unverified" : "unchanged";
        result.summary = Json{{"message", request.rerun_tests ? "patch candidate applied but no rerun command was provided" : "patch candidate applied"},
                              {"iterations", result.iterations}};
        return domain::AppResult<RepairWorkflowResult>::success(std::move(result));
    }

    result.iterations = iterations;
    result.status = "not_repaired";
    if (result.final_workspace_state.empty()) result.final_workspace_state = result.restored ? "restored" : "unchanged";
    result.summary = Json{{"message", "no patch candidate repaired the failing tests"},
                          {"iterations", result.iterations},
                          {"max_iterations", max_iterations},
                          {"restored", result.restored},
                          {"final_workspace_state", result.final_workspace_state}};
    return domain::AppResult<RepairWorkflowResult>::success(std::move(result));
}

Json to_json(const RepairWorkflowResult& result) {
    return Json{{"success", result.success},
                {"provider", "diagnostic_repair_workflow"},
                {"status", result.status},
                {"iterations", result.iterations},
                {"checkpoint_id", result.checkpoint_id},
                {"restored", result.restored},
                {"restore_reason", result.restore_reason},
                {"final_workspace_state", result.final_workspace_state},
                {"summary", result.summary},
                {"repair_plan", result.repair_plan},
                {"attempts", result.attempts},
                {"final_test_result", result.final_test_result},
                {"safety", result.safety}};
}

} // namespace ben_gear::diagnostic_repair
