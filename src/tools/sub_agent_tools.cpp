#include "ben_gear/tools/sub_agent_tools.hpp"
#include "ben_gear/agent/agent.hpp"
#include "ben_gear/agent/shared_resources.hpp"
#include "ben_gear/base/net/event_loop.hpp"
#include "ben_gear/base/net/io_context.hpp"
#include "ben_gear/base/log/logger.hpp"
#include "ben_gear/workspace/uuid.hpp"

namespace ben_gear::tools {

using namespace agent;

void register_sub_agent_tools(
    llm::ToolRegistry& registry,
    std::shared_ptr<agent::SubAgentRuntime> runtime) {

    registry.register_tool(
        container::String("delegate_task"),
        container::String(
            "委派任务给子 Agent 执行。子 Agent 拥有独立的会话上下文和工具集，"
            "可以自主调用工具完成复杂子任务。"
            "子 Agent 无法再委派子 Agent（禁止递归）。"),
        {
            {"prompt", {"string", "任务描述（必填）", {}, true}},
            {"system_prompt", {"string", "覆盖系统提示（可选）", {}, false}},
            {"tool_filter", {"array", "限制可用工具列表（可选，空=全部）", {}, false}},
            {"max_steps", {"integer", "最大工具调用步数（可选，默认20）", {}, false}},
            {"timeout_seconds", {"integer", "超时秒数（可选，默认120）", {}, false}},
            {"speculative_models", {"array", "推测执行模型列表（可选）", {}, false}},
        },
        [runtime](const Json& args) -> container::String {
            log::info_fmt("delegate_task: invoked, prompt_size={}",
                          args.contains("prompt") && args["prompt"].is_string()
                              ? args["prompt"].get<std::string>().size() : 0);

            if (!args.contains("prompt") || !args["prompt"].is_string()) {
                log::error_fmt("delegate_task: missing or invalid prompt argument");
                return container::String("{\"success\":false,\"error\":\"prompt required\"}");
            }

            SubAgentTask task;
            task.prompt = container::String(args["prompt"].get<std::string>());

            if (args.contains("system_prompt") && args["system_prompt"].is_string()) {
                task.system_prompt = container::String(args["system_prompt"].get<std::string>());
            }
            if (args.contains("tool_filter") && args["tool_filter"].is_array()) {
                for (const auto& t : args["tool_filter"]) {
                    if (t.is_string()) task.tool_filter.push_back(container::String(t.get<std::string>()));
                }
            }
            if (args.contains("max_steps") && args["max_steps"].is_number_integer()) {
                task.max_steps = args["max_steps"].get<int>();
            }
            if (args.contains("timeout_seconds") && args["timeout_seconds"].is_number_integer()) {
                task.timeout = std::chrono::seconds(args["timeout_seconds"].get<int>());
            }
            if (args.contains("speculative_models") && args["speculative_models"].is_array()) {
                for (const auto& m : args["speculative_models"]) {
                    if (m.is_string()) task.speculative_models.push_back(container::String(m.get<std::string>()));
                }
            }

            auto& wf_loop = runtime->resources()->wf_context()->loop();
            net::CancellationToken cancel;

            try {
                auto coro = runtime->execute(std::move(task), cancel);
                auto result = net::sync_wait(wf_loop, std::move(coro));

                Json ret;
                ret["success"] = result.success;
                ret["output"] = std::string(result.output.data(), result.output.size());
                if (!result.error.empty()) {
                    ret["error"] = std::string(result.error.data(), result.error.size());
                }
                ret["tool_steps"] = result.tool_steps;
                ret["was_truncated"] = result.was_truncated;
                ret["usage"] = Json::object();
                ret["usage"]["prompt_tokens"] = result.usage.prompt_tokens;
                ret["usage"]["completion_tokens"] = result.usage.completion_tokens;
                ret["usage"]["total_tokens"] = result.usage.total_tokens;
                if (!result.artifacts.is_null()) ret["artifacts"] = result.artifacts;

                return container::String(ret.dump());
            } catch (const std::exception& e) {
                Json err;
                err["success"] = false;
                err["error"] = std::string("delegate_task failed: ") + e.what();
                return container::String(err.dump());
            }
        },
        false
    );

    registry.register_tool(
        container::String("delegate_tasks"),
        container::String(
            "并行委派多个任务给子 Agent 执行。每个子 Agent 独立运行，"
            "结果全部收集后返回。并行数受 max_parallel 配置限制（默认5）。"),
        {
            {"tasks", {"array", "任务数组，每项含 prompt 及可选参数", {}, true}},
            {"max_steps", {"integer", "全局最大步数覆盖（可选）", {}, false}},
            {"timeout_seconds", {"integer", "全局超时秒数覆盖（可选）", {}, false}},
        },
        [runtime](const Json& args) -> container::String {
            log::info_fmt("delegate_tasks: invoked, tasks_count={}",
                          args.contains("tasks") && args["tasks"].is_array()
                              ? args["tasks"].size() : 0);

            if (!args.contains("tasks") || !args["tasks"].is_array() || args["tasks"].empty()) {
                log::error_fmt("delegate_tasks: missing or invalid tasks argument");
                return container::String("{\"success\":false,\"error\":\"tasks array required\"}");
            }

            container::Vector<SubAgentTask> tasks;
            int global_max_steps = 0;
            if (args.contains("max_steps") && args["max_steps"].is_number_integer()) {
                global_max_steps = args["max_steps"].get<int>();
            }
            std::chrono::milliseconds global_timeout{0};
            if (args.contains("timeout_seconds") && args["timeout_seconds"].is_number_integer()) {
                global_timeout = std::chrono::seconds(args["timeout_seconds"].get<int>());
            }

            for (const auto& t : args["tasks"]) {
                if (!t.contains("prompt") || !t["prompt"].is_string()) continue;
                SubAgentTask task;
                task.prompt = container::String(t["prompt"].get<std::string>());
                if (t.contains("system_prompt") && t["system_prompt"].is_string()) {
                    task.system_prompt = container::String(t["system_prompt"].get<std::string>());
                }
                task.max_steps = global_max_steps;
                if (t.contains("max_steps") && t["max_steps"].is_number_integer()) {
                    task.max_steps = t["max_steps"].get<int>();
                }
                task.timeout = global_timeout;
                if (t.contains("timeout_seconds") && t["timeout_seconds"].is_number_integer()) {
                    task.timeout = std::chrono::seconds(t["timeout_seconds"].get<int>());
                }
                tasks.push_back(std::move(task));
            }

            auto& wf_loop = runtime->resources()->wf_context()->loop();
            net::CancellationToken cancel;

            try {
                auto coro = runtime->execute_parallel(std::move(tasks), cancel);
                auto results = net::sync_wait(wf_loop, std::move(coro));

                Json ret = Json::array();
                for (const auto& r : results) {
                    Json item;
                    item["success"] = r.success;
                    item["output"] = std::string(r.output.data(), r.output.size());
                    if (!r.error.empty()) item["error"] = std::string(r.error.data(), r.error.size());
                    item["tool_steps"] = r.tool_steps;
                    item["was_truncated"] = r.was_truncated;
                    ret.push_back(item);
                }
                return container::String(ret.dump());
            } catch (const std::exception& e) {
                Json err;
                err["success"] = false;
                err["error"] = std::string("delegate_tasks failed: ") + e.what();
                return container::String(err.dump());
            }
        },
        false
    );

    log::info_fmt("registered sub-agent tools: delegate_task, delegate_tasks");
}

} // namespace ben_gear::tools
