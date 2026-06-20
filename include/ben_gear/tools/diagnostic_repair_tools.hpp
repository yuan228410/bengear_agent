#pragma once

#include "ben_gear/diagnostic_repair/diagnostic_repair_plan_service.hpp"
#include "ben_gear/tool/registry.hpp"

#include <memory>

namespace ben_gear::tools {

inline void register_diagnostic_repair_tools(llm::ToolRegistry& registry,
                                             std::shared_ptr<diagnostic_repair::DiagnosticRepairPlanService> service) {
    if (!service) return;

    registry.register_tool(
        base::container::String("diagnostic_repair_plan"),
        base::container::String("Build a deterministic read-only repair plan preview for structured test diagnostics."),
        {{base::container::String("diagnostics"), {base::container::String("array"), base::container::String("Structured diagnostics from run_tests"), {}, false}},
         {base::container::String("output"), {base::container::String("string"), base::container::String("Optional raw test output to parse when diagnostics are absent"), {}, false}},
         {base::container::String("cwd"), {base::container::String("string"), base::container::String("Workspace-relative command cwd used for diagnostics"), {}, false}},
         {base::container::String("context_lines"), {base::container::String("integer"), base::container::String("Source lines before and after each diagnostic"), {}, false}},
         {base::container::String("max_diagnostics"), {base::container::String("integer"), base::container::String("Maximum diagnostics to include"), {}, false}},
         {base::container::String("max_file_bytes"), {base::container::String("integer"), base::container::String("Maximum bytes to read per file"), {}, false}},
         {base::container::String("max_total_bytes"), {base::container::String("integer"), base::container::String("Approximate total snippet byte budget"), {}, false}},
         {base::container::String("include_code_intel"), {base::container::String("boolean"), base::container::String("Include best-effort indexed symbols and definitions"), {}, false}}},
        [service](const Json& args) -> base::container::String {
            auto result = service->repair_plan(args).dump();
            return base::container::String(result.c_str(), result.size());
        },
        true);
}

} // namespace ben_gear::tools
