#include "capabilities/tool/test_loop_tools.hpp"

#include "application/command_descriptor_factory.hpp"
#include "application/request_context.hpp"
#include "capabilities/test_loop/test_loop_service.hpp"
#include "capabilities/tool/registry.hpp"
#include "capabilities/tool/command_tool_helpers.hpp"

#include <memory>
#include <string>

namespace ben_gear::tools {

void register_test_loop_tools(llm::ToolRegistry& registry,
                                     std::shared_ptr<test_loop::TestLoopService> service,
                                     application::CommandPipeline command_pipeline,
                                     application::RequestContext request,
                                     std::string project_path) {
    if (!service) return;

    registry.register_tool(
        std::string("inspect_test_commands"),
        std::string("Inspect the workspace and suggest likely build/test commands. Read-only."),
        {},
        [service](const Json&) -> std::string {
            auto result = service->inspect();
            if (!result.ok()) return command_detail::json_tool_output(command_detail::app_error_to_json(result.error()));
            return command_detail::json_tool_output(test_loop::to_json(result.value()));
        },
        true);

    registry.register_tool(
        std::string("run_tests"),
        std::string("Run a build or test command inside the workspace and return structured output and failure summary."),
        {{std::string("command"), {std::string("string"), std::string("Build or test command to execute"), {}, true}},
         {std::string("cwd"), {std::string("string"), std::string("Optional working directory inside the workspace"), {}, false}},
         {std::string("timeout_seconds"), {std::string("integer"), std::string("Timeout in seconds; default 120, max 3600"), {}, false}},
         {std::string("max_output_bytes"), {std::string("integer"), std::string("Output truncation limit; default 60000"), {}, false}}},
        [service, command_pipeline, request, project_path](const Json& args) -> std::string {
            auto command_text = args.value("command", "");
            auto cwd = args.value("cwd", "");
            int timeout_seconds = args.value("timeout_seconds", 120);
            int max_output_bytes = args.value("max_output_bytes", 60000);

            auto command = application::CommandDescriptorFactory(request, project_path)
                               .test_run(command_text, cwd, timeout_seconds, max_output_bytes);

            return command_detail::pipeline_tool_output(command_pipeline.execute<Json>(command, [&]() {
                auto result = service->run(command_text, cwd, timeout_seconds, max_output_bytes);
                if (!result.ok()) return domain::AppResult<Json>::failure(result.error());
                return domain::AppResult<Json>::success(test_loop::to_json(result.value()));
            }));
        });
}

} // namespace ben_gear::tools
