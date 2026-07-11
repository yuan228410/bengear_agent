#pragma once

#include "intelligence/diagnostic_repair/diagnostic_repair_patch_preview_service.hpp"
#include "intelligence/diagnostic_repair/diagnostic_repair_plan_service.hpp"
#include "intelligence/diagnostic_repair/diagnostic_repair_workflow_service.hpp"
#include "tool/registry.hpp"
#include "tool/command_tool_helpers.hpp"

#include <memory>

namespace ben_gear::tools {

inline auto diagnostic_repair_parameters(bool include_patch_preview) {
    base::container::Vector<std::pair<base::container::String, llm::ToolParameterSchema>> params{
        {base::container::String("diagnostics"), {base::container::String("array"), base::container::String("Structured diagnostics from run_tests"), {}, false}},
        {base::container::String("output"), {base::container::String("string"), base::container::String("Optional raw test output to parse when diagnostics are absent"), {}, false}},
        {base::container::String("cwd"), {base::container::String("string"), base::container::String("Workspace-relative command cwd used for diagnostics"), {}, false}},
        {base::container::String("context_lines"), {base::container::String("integer"), base::container::String("Source lines before and after each diagnostic"), {}, false}},
        {base::container::String("max_diagnostics"), {base::container::String("integer"), base::container::String("Maximum diagnostics to include"), {}, false}},
        {base::container::String("max_file_bytes"), {base::container::String("integer"), base::container::String("Maximum bytes to read per file"), {}, false}},
        {base::container::String("max_total_bytes"), {base::container::String("integer"), base::container::String("Approximate total snippet byte budget"), {}, false}},
        {base::container::String("include_code_intel"), {base::container::String("boolean"), base::container::String("Include best-effort indexed symbols and definitions"), {}, false}},
        {base::container::String("failure_category"), {base::container::String("string"), base::container::String("Failure category from run_tests: build, test, environment, timeout, or unknown"), {}, false}},
        {base::container::String("command"), {base::container::String("string"), base::container::String("Original test command to rerun after repair"), {}, false}},
        {base::container::String("timeout_seconds"), {base::container::String("integer"), base::container::String("Original test timeout in seconds"), {}, false}},
        {base::container::String("max_output_bytes"), {base::container::String("integer"), base::container::String("Original output byte budget"), {}, false}},
    };
    if (include_patch_preview) {
        params.push_back({base::container::String("unified_diff"), {base::container::String("string"), base::container::String("Candidate unified diff to validate without applying"), {}, true}});
        params.push_back({base::container::String("plan_id"), {base::container::String("string"), base::container::String("Optional repair plan id to compare touched files against"), {}, false}});
        params.push_back({base::container::String("max_diff_bytes"), {base::container::String("integer"), base::container::String("Maximum candidate diff bytes"), {}, false}});
    }
    return params;
}

inline auto diagnostic_repair_workflow_parameters() {
    auto params = diagnostic_repair_parameters(false);
    params.push_back({base::container::String("unified_diff"), {base::container::String("string"), base::container::String("Single candidate unified diff; optional when patch_candidates is provided"), {}, false}});
    params.push_back({base::container::String("plan_id"), {base::container::String("string"), base::container::String("Optional repair plan id to compare touched files against"), {}, false}});
    params.push_back({base::container::String("patch_candidates"), {base::container::String("array"), base::container::String("Candidate patch objects with id, unified_diff, and description"), {}, false}});
    params.push_back({base::container::String("username"), {base::container::String("string"), base::container::String("Request username for command governance"), {}, false}});
    params.push_back({base::container::String("workspace"), {base::container::String("string"), base::container::String("Workspace name for command governance"), {}, false}});
    params.push_back({base::container::String("session_id"), {base::container::String("string"), base::container::String("Session id for command governance and patch audit"), {}, false}});
    params.push_back({base::container::String("max_iterations"), {base::container::String("integer"), base::container::String("Maximum candidate attempts, clamped to 1..5"), {}, false}});
    params.push_back({base::container::String("apply_patch"), {base::container::String("boolean"), base::container::String("Apply the first safe candidate patch; default true"), {}, false}});
    params.push_back({base::container::String("rerun_tests"), {base::container::String("boolean"), base::container::String("Rerun the recommended test command after applying a patch; default true"), {}, false}});
    params.push_back({base::container::String("checkpoint_before_apply"), {base::container::String("boolean"), base::container::String("Create a checkpoint before applying each candidate patch; default true"), {}, false}});
    params.push_back({base::container::String("restore_on_failure"), {base::container::String("boolean"), base::container::String("Restore the candidate checkpoint when rerun fails; default true"), {}, false}});
    params.push_back({base::container::String("checkpoint_label"), {base::container::String("string"), base::container::String("Optional checkpoint description used before applying candidate patches"), {}, false}});
    return params;
}

inline void register_diagnostic_repair_tools(
    llm::ToolRegistry& registry,
    std::shared_ptr<diagnostic_repair::DiagnosticRepairPlanService> service,
    std::shared_ptr<diagnostic_repair::DiagnosticRepairPatchPreviewService> patch_preview_service = nullptr,
    std::shared_ptr<diagnostic_repair::DiagnosticRepairWorkflowService> workflow_service = nullptr) {
    if (service) {
        registry.register_tool(
            base::container::String("diagnostic_repair_plan"),
            base::container::String("Build a deterministic read-only repair plan preview for structured test diagnostics."),
            diagnostic_repair_parameters(false),
            [service](const Json& args) -> base::container::String {
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
            base::container::String("diagnostic_repair_workflow"),
            base::container::String("Run a governed diagnostic repair workflow: plan, preview candidate patches, apply a safe patch, rerun recommended tests, and summarize."),
            diagnostic_repair_workflow_parameters(),
            [workflow_service](const Json& args) -> base::container::String {
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
            base::container::String("diagnostic_repair_patch_preview"),
            base::container::String("Validate a candidate diagnostic repair unified diff without applying it."),
            diagnostic_repair_parameters(true),
            [patch_preview_service](const Json& args) -> base::container::String {
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
