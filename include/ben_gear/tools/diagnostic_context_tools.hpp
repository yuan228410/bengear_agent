#pragma once

#include "ben_gear/diagnostic_context/diagnostic_context_service.hpp"
#include "ben_gear/tool/registry.hpp"
#include "ben_gear/tools/command_tool_helpers.hpp"

#include <memory>

namespace ben_gear::tools {

inline void register_diagnostic_context_tools(llm::ToolRegistry& registry,
                                              std::shared_ptr<diagnostic_context::DiagnosticContextService> service) {
    if (!service) return;

    registry.register_tool(
        base::container::String("diagnostic_repair_context"),
        base::container::String("Build bounded source repair context for structured test diagnostics. Read-only."),
        {{base::container::String("diagnostics"), {base::container::String("array"), base::container::String("Structured diagnostics from run_tests"), {}, false}},
         {base::container::String("output"), {base::container::String("string"), base::container::String("Optional raw test output to parse when diagnostics are absent"), {}, false}},
         {base::container::String("cwd"), {base::container::String("string"), base::container::String("Workspace-relative command cwd used for diagnostics"), {}, false}},
         {base::container::String("context_lines"), {base::container::String("integer"), base::container::String("Source lines before and after each diagnostic"), {}, false}},
         {base::container::String("max_diagnostics"), {base::container::String("integer"), base::container::String("Maximum diagnostics to include"), {}, false}},
         {base::container::String("max_file_bytes"), {base::container::String("integer"), base::container::String("Maximum bytes to read per file"), {}, false}},
         {base::container::String("max_total_bytes"), {base::container::String("integer"), base::container::String("Approximate total snippet byte budget"), {}, false}},
         {base::container::String("include_code_intel"), {base::container::String("boolean"), base::container::String("Include best-effort indexed symbols and definitions"), {}, false}}},
        [service](const Json& args) -> base::container::String {
            auto result = command_detail::app_result_json(
                service->repair_context(args),
                [](const diagnostic_context::RepairContextResult& value) {
                    return diagnostic_context::to_json(value);
                });
            return command_detail::json_tool_output(result);
        },
        true);
}

} // namespace ben_gear::tools
