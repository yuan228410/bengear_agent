#pragma once

#include "ben_gear/diagnostic_repair/diagnostic_repair_patch_preview_service.hpp"
#include "ben_gear/diagnostic_repair/diagnostic_repair_plan_service.hpp"
#include "ben_gear/tool/registry.hpp"
#include "ben_gear/tools/command_tool_helpers.hpp"

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
    };
    if (include_patch_preview) {
        params.push_back({base::container::String("unified_diff"), {base::container::String("string"), base::container::String("Candidate unified diff to validate without applying"), {}, true}});
        params.push_back({base::container::String("plan_id"), {base::container::String("string"), base::container::String("Optional repair plan id to compare touched files against"), {}, false}});
        params.push_back({base::container::String("max_diff_bytes"), {base::container::String("integer"), base::container::String("Maximum candidate diff bytes"), {}, false}});
    }
    return params;
}

inline void register_diagnostic_repair_tools(
    llm::ToolRegistry& registry,
    std::shared_ptr<diagnostic_repair::DiagnosticRepairPlanService> service,
    std::shared_ptr<diagnostic_repair::DiagnosticRepairPatchPreviewService> patch_preview_service = nullptr) {
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
