#include "capabilities/tool/diagnostic_context_tools.hpp"

#include "intelligence/diagnostic_context/diagnostic_context_service.hpp"
#include "capabilities/tool/registry.hpp"
#include "capabilities/tool/command_tool_helpers.hpp"

#include <memory>

namespace ben_gear::tools {

void register_diagnostic_context_tools(llm::ToolRegistry& registry,
                                              std::shared_ptr<diagnostic_context::DiagnosticContextService> service) {
    if (!service) return;

    registry.register_tool(
        std::string("diagnostic_repair_context"),
        std::string("Build bounded source repair context for structured test diagnostics. Read-only."),
        {{std::string("diagnostics"), {std::string("array"), std::string("Structured diagnostics from run_tests"), {}, false}},
         {std::string("output"), {std::string("string"), std::string("Optional raw test output to parse when diagnostics are absent"), {}, false}},
         {std::string("cwd"), {std::string("string"), std::string("Workspace-relative command cwd used for diagnostics"), {}, false}},
         {std::string("context_lines"), {std::string("integer"), std::string("Source lines before and after each diagnostic"), {}, false}},
         {std::string("max_diagnostics"), {std::string("integer"), std::string("Maximum diagnostics to include"), {}, false}},
         {std::string("max_file_bytes"), {std::string("integer"), std::string("Maximum bytes to read per file"), {}, false}},
         {std::string("max_total_bytes"), {std::string("integer"), std::string("Approximate total snippet byte budget"), {}, false}},
         {std::string("include_code_intel"), {std::string("boolean"), std::string("Include best-effort indexed symbols and definitions"), {}, false}}},
        [service](const Json& args) -> std::string {
            auto request = diagnostic_context::repair_context_request_from_json(args);
            if (!request.ok()) return command_detail::json_tool_output(command_detail::app_error_to_json(request.error()));
            auto result = command_detail::app_result_json(
                service->repair_context(std::move(request.value())),
                [](const diagnostic_context::RepairContextResult& value) {
                    return diagnostic_context::to_json(value);
                });
            return command_detail::json_tool_output(result);
        },
        true);
}

} // namespace ben_gear::tools
