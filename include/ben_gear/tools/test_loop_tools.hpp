#pragma once

#include "ben_gear/application/request_context.hpp"
#include "ben_gear/test_loop/test_loop_service.hpp"
#include "ben_gear/tool/registry.hpp"
#include "ben_gear/tools/command_tool_helpers.hpp"

#include <memory>
#include <string>

namespace ben_gear::tools {

inline void register_test_loop_tools(llm::ToolRegistry& registry,
                                     std::shared_ptr<test_loop::TestLoopService> service,
                                     application::CommandPipeline command_pipeline = application::CommandPipeline(),
                                     application::RequestContext request = {},
                                     base::container::String project_path = base::container::String()) {
    if (!service) return;

    registry.register_tool(
        base::container::String("inspect_test_commands"),
        base::container::String("Inspect the workspace and suggest likely build/test commands. Read-only."),
        {},
        [service](const Json&) -> base::container::String {
            auto result = service->inspect().dump();
            return base::container::String(result.c_str(), result.size());
        },
        true);

    registry.register_tool(
        base::container::String("run_tests"),
        base::container::String("Run a build or test command inside the workspace and return structured output and failure summary."),
        {{base::container::String("command"), {base::container::String("string"), base::container::String("Build or test command to execute"), {}, true}},
         {base::container::String("cwd"), {base::container::String("string"), base::container::String("Optional working directory inside the workspace"), {}, false}},
         {base::container::String("timeout_seconds"), {base::container::String("integer"), base::container::String("Timeout in seconds; default 120, max 3600"), {}, false}},
         {base::container::String("max_output_bytes"), {base::container::String("integer"), base::container::String("Output truncation limit; default 60000"), {}, false}}},
        [service, command_pipeline, request, project_path](const Json& args) -> base::container::String {
            auto command_text = args.value("command", "");
            auto cwd = args.value("cwd", "");
            int timeout_seconds = args.value("timeout_seconds", 120);
            int max_output_bytes = args.value("max_output_bytes", 60000);

            application::CommandDescriptor command;
            command.action = base::container::String("test.run");
            command.username = request.username;
            command.workspace_name = request.workspace_name;
            command.session_id = request.session_id;
            command.project_path = project_path;
            command.subject = base::container::String(command_text.c_str());
            command.risk = application::CommandRisk::command_execution;
            command.runs_command = true;
            command.timeout_seconds = timeout_seconds;
            command.max_output_bytes = max_output_bytes;
            command.working_directory = base::container::String(cwd.c_str());

            return command_detail::pipeline_tool_output(command_pipeline.execute<Json>(command, [&]() {
                return command_detail::json_command_result(service->run(command_text, cwd, timeout_seconds, max_output_bytes),
                                                           "test_run_failed",
                                                           "test command failed");
            }));
        });
}

} // namespace ben_gear::tools
