#pragma once

#include "ben_gear/test_loop/test_loop_service.hpp"
#include "ben_gear/tool/registry.hpp"

#include <memory>

namespace ben_gear::tools {

inline void register_test_loop_tools(llm::ToolRegistry& registry,
                                     std::shared_ptr<test_loop::TestLoopService> service) {
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
        [service](const Json& args) -> base::container::String {
            auto command = args.value("command", "");
            auto cwd = args.value("cwd", "");
            int timeout_seconds = args.value("timeout_seconds", 120);
            int max_output_bytes = args.value("max_output_bytes", 60000);
            auto result = service->run(command, cwd, timeout_seconds, max_output_bytes).dump();
            return base::container::String(result.c_str(), result.size());
        });
}

} // namespace ben_gear::tools
