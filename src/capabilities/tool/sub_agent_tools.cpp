#include "capabilities/tool/sub_agent_tools.hpp"
#include "capabilities/tool/registry.hpp"
#include "agent/sub_agent_types.hpp"
#include "agent/runtime/sub_agent_runtime.hpp"
#include "base/utils/json.hpp"
#include "base/log/logger.hpp"

namespace ben_gear::tools {

void register_sub_agent_tools(
    capabilities::tool::ToolRegistry& registry,
    std::shared_ptr<agent::runtime::SubAgentRuntime> runtime) {

    registry.register_tool(
        std::string("delegate_task"),
        std::string("Delegate a single task to a sub-agent. Returns full result."),
        {
            {std::string("prompt"),
             {std::string("string"),
              std::string("The task prompt to delegate"), {}, true}},
            {std::string("system_prompt"),
             {std::string("string"),
              std::string("Optional system prompt override"), {}, false}},
            {std::string("timeout"),
             {std::string("integer"),
              std::string("Timeout in milliseconds"), {}, false}},
        },
        [runtime](const Json& args) -> std::string {
            agent::SubAgentTask task;
            task.id = "single_" + std::to_string(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
            task.prompt = args["prompt"].get<std::string>();

            if (args.contains("system_prompt")) {
                task.system_prompt = args["system_prompt"].get<std::string>();
            }
            if (args.contains("timeout")) {
                task.timeout = std::chrono::milliseconds(args["timeout"].get<int>());
            }

            auto config = runtime->default_config();

            try {
                auto result = runtime->execute(runtime->loop(), task, config);
                return Json{
                    {"success", result.success},
                    {"task_id", result.task_id},
                    {"output", result.output},
                    {"full_output", result.full_output},
                    {"error", result.error},
                    {"duration_ms", result.duration.count()},
                    {"tool_calls", result.tool_calls},
                    {"was_truncated", result.was_truncated},
                    {"was_summarized", result.was_summarized},
                    {"status", static_cast<int>(result.status)}
                }.dump();
            } catch (const std::exception& e) {
                return Json{
                    {"success", false},
                    {"error", std::string(e.what())}
                }.dump();
            }
        });

    registry.register_tool(
        std::string("delegate_tasks"),
        std::string("Delegate multiple tasks to parallel sub-agents. Returns array of results."),
        {
            {std::string("prompts"),
             {std::string("array"),
              std::string("List of task prompts, one per sub-agent"), {}, true}},
            {std::string("max_parallel"),
             {std::string("integer"),
              std::string("Maximum sub-agents to run concurrently (default: 5)"), {}, false}},
            {std::string("auto_summary"),
             {std::string("boolean"),
              std::string("Automatically summarize results (default: true)"), {}, false}},
        },
        [runtime](const Json& args) -> std::string {
            std::vector<agent::SubAgentTask> tasks;
            if (args.contains("prompts") && args["prompts"].is_array()) {
                for (size_t i = 0; i < args["prompts"].size(); ++i) {
                    agent::SubAgentTask task;
                    task.id = "parallel_" + std::to_string(i) + "_" + std::to_string(
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch()).count());
                    if (args["prompts"][i].is_string()) {
                        task.prompt = args["prompts"][i].get<std::string>();
                    } else if (args["prompts"][i].is_object()) {
                        const auto& obj = args["prompts"][i];
                        task.prompt = obj["prompt"].get<std::string>();
                        if (obj.contains("system_prompt")) {
                            task.system_prompt = obj["system_prompt"].get<std::string>();
                        }
                        if (obj.contains("id")) {
                            task.id = obj["id"].get<std::string>();
                        }
                    }
                    tasks.push_back(std::move(task));
                }
            }
            if (tasks.empty()) {
                return Json{{"success", false}, {"error", "prompts array is empty"}}.dump();
            }

            int max_parallel = args.value("max_parallel", runtime->default_config().max_parallel);
            if (max_parallel <= 0) max_parallel = 1;

            auto config = runtime->default_config();
            if (args.contains("auto_summary")) {
                config.auto_summary = args["auto_summary"].get<bool>();
            }

            try {
                auto results = runtime->execute_parallel(
                    runtime->loop(), tasks, config, max_parallel);

                Json output = Json::array();
                for (const auto& r : results) {
                    output.push_back(Json{
                        {"success", r.success},
                        {"task_id", r.task_id},
                        {"output", r.output},
                        {"full_output", r.full_output},
                        {"error", r.error},
                        {"duration_ms", r.duration.count()},
                        {"tool_calls", r.tool_calls},
                        {"was_truncated", r.was_truncated},
                        {"was_summarized", r.was_summarized},
                        {"status", static_cast<int>(r.status)}
                    });
                }
                return Json{{"results", output}, {"total", static_cast<int>(results.size())}}.dump();
            } catch (const std::exception& e) {
                return Json{
                    {"success", false},
                    {"error", std::string(e.what())}
                }.dump();
            }
        });

    log::info_fmt("sub_agent_tools: registered delegate_task and delegate_tasks");
}

} // namespace ben_gear::tools
