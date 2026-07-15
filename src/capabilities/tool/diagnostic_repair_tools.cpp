#include "capabilities/tool/diagnostic_repair_tools.hpp"

#include "intelligence/diagnostic_repair/diagnostic_repair_patch_preview_service.hpp"
#include "intelligence/diagnostic_repair/diagnostic_repair_plan_service.hpp"
#include "intelligence/diagnostic_repair/diagnostic_repair_workflow_service.hpp"
#include "capabilities/tool/registry.hpp"
#include "capabilities/tool/command_tool_helpers.hpp"

#include <memory>

namespace ben_gear::tools {

auto diagnostic_repair_parameters(bool include_patch_preview) {
    std::vector<std::pair<std::string, llm::ToolParameterSchema>> params{
        {std::string("diagnostics"), {std::string("array"), std::string("Structured diagnostics from run_tests"), {}, false}},
        {std::string("output"), {std::string("string"), std::string("Optional raw test output to parse when diagnostics are absent"), {}, false}},
        {std::string("cwd"), {std::string("string"), std::string("Workspace-relative command cwd used for diagnostics"), {}, false}},
        {std::string("context_lines"), {std::string("integer"), std::string("Source lines before and after each diagnostic"), {}, false}},
        {std::string("max_diagnostics"), {std::string("integer"), std::string("Maximum diagnostics to include"), {}, false}},
        {std::string("max_file_bytes"), {std::string("integer"), std::string("Maximum bytes to read per file"), {}, false}},
        {std::string("max_total_bytes"), {std::string("integer"), std::string("Approximate total snippet byte budget"), {}, false}},
        {std::string("include_code_intel"), {std::string("boolean"), std::string("Include best-effort indexed symbols and definitions"), {}, false}},
        {std::string("failure_category"), {std::string("string"), std::string("Failure category from run_tests: build, test, environment, timeout, or unknown"), {}, false}},
        {std::string("command"), {std::string("string"), std::string("Original test command to rerun after repair"), {}, false}},
        {std::string("timeout_seconds"), {std::string("integer"), std::string("Original test timeout in seconds"), {}, false}},
        {std::string("max_output_bytes"), {std::string("integer"), std::string("Original output byte budget"), {}, false}},
    };
    if (include_patch_preview) {
        params.push_back({std::string("unified_diff"), {std::string("string"), std::string("Candidate unified diff to validate without applying"), {}, true}});
        params.push_back({std::string("plan_id"), {std::string("string"), std::string("Optional repair plan id to compare touched files against"), {}, false}});
        params.push_back({std::string("max_diff_bytes"), {std::string("integer"), std::string("Maximum candidate diff bytes"), {}, false}});
    }
    return params;
}

auto diagnostic_repair_workflow_parameters() {
    auto params = diagnostic_repair_parameters(false);
    params.push_back({std::string("unified_diff"), {std::string("string"), std::string("Single candidate unified diff; optional when patch_candidates is provided"), {}, false}});
    params.push_back({std::string("plan_id"), {std::string("string"), std::string("Optional repair plan id to compare touched files against"), {}, false}});
    params.push_back({std::string("patch_candidates"), {std::string("array"), std::string("Candidate patch objects with id, unified_diff, and description"), {}, false}});
    params.push_back({std::string("username"), {std::string("string"), std::string("Request username for command governance"), {}, false}});
    params.push_back({std::string("workspace"), {std::string("string"), std::string("Workspace name for command governance"), {}, false}});
    params.push_back({std::string("session_id"), {std::string("string"), std::string("Session id for command governance and patch audit"), {}, false}});
    params.push_back({std::string("max_iterations"), {std::string("integer"), std::string("Maximum candidate attempts, clamped to 1..5"), {}, false}});
    params.push_back({std::string("apply_patch"), {std::string("boolean"), std::string("Apply the first safe candidate patch; default true"), {}, false}});
    params.push_back({std::string("rerun_tests"), {std::string("boolean"), std::string("Rerun the recommended test command after applying a patch; default true"), {}, false}});
    params.push_back({std::string("checkpoint_before_apply"), {std::string("boolean"), std::string("Create a checkpoint before applying each candidate patch; default true"), {}, false}});
    params.push_back({std::string("restore_on_failure"), {std::string("boolean"), std::string("Restore the candidate checkpoint when rerun fails; default true"), {}, false}});
    params.push_back({std::string("checkpoint_label"), {std::string("string"), std::string("Optional checkpoint description used before applying candidate patches"), {}, false}});
    return params;
}

void register_diagnostic_repair_tools(
    llm::ToolRegistry& registry,
    std::shared_ptr<diagnostic_repair::DiagnosticRepairPlanService> service,
    std::shared_ptr<diagnostic_repair::DiagnosticRepairPatchPreviewService> patch_preview_service,
    std::shared_ptr<diagnostic_repair::DiagnosticRepairWorkflowService> workflow_service) {
    if (service) {
        registry.register_tool(
            std::string("diagnostic_repair_plan"),
            std::string("Build a deterministic read-only repair plan preview for structured test diagnostics."),
            diagnostic_repair_parameters(false),
            [service](const Json& args) -> std::string {
                auto request = diagnostic_repair::repair_plan_request_from_json(args);
                if (!request.ok()) return command_detail::json_tool_output(command_detail::app_error_to_json(request.error()));
                auto result = command_detail::app_result_json(
                    service->repair_plan(std::move(request.value())),
                    [](const diagnostic_repair::RepairPlanResult& value) {
                        return diagnostic_repair::to_json(value);
                    });
                return command_detail::json_tool_output(result);
            },
            true);
    }

    if (workflow_service) {
        registry.register_tool(
            std::string("diagnostic_repair_workflow"),
            std::string("Run a governed diagnostic repair workflow: plan, preview candidate patches, apply a safe patch, rerun recommended tests, and summarize."),
            diagnostic_repair_workflow_parameters(),
            [workflow_service](const Json& args) -> std::string {
                auto request = diagnostic_repair::repair_workflow_request_from_json(args);
                if (!request.ok()) return command_detail::json_tool_output(command_detail::app_error_to_json(request.error()));
                auto result = command_detail::app_result_json(
                    workflow_service->repair_workflow(request.value()),
                    [](const diagnostic_repair::RepairWorkflowResult& value) {
                        return diagnostic_repair::to_json(value);
                    });
                return command_detail::json_tool_output(result);
            },
            false);
    }

    if (patch_preview_service) {
        registry.register_tool(
            std::string("diagnostic_repair_patch_preview"),
            std::string("Validate a candidate diagnostic repair unified diff without applying it."),
            diagnostic_repair_parameters(true),
            [patch_preview_service](const Json& args) -> std::string {
                auto request = diagnostic_repair::repair_patch_preview_request_from_json(args);
                if (!request.ok()) return command_detail::json_tool_output(command_detail::app_error_to_json(request.error()));
                auto result = command_detail::app_result_json(
                    patch_preview_service->repair_patch_preview(std::move(request.value())),
                    [](const diagnostic_repair::RepairPatchPreviewResult& value) {
                        return diagnostic_repair::to_json(value);
                    });
                return command_detail::json_tool_output(result);
            },
            true);
    }
}

} // namespace ben_gear::tools
