#include "agent/runtime/sub_agent_tools.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "capabilities/tool/registry.hpp"
#include "agent/sub_agent_types.hpp"
#include "agent/runtime/sub_agent_runtime.hpp"
#include "base/utils/json.hpp"
#include "log/logger.hpp"

namespace ben_gear::tools {

namespace {

/// 将 SubAgentResult 格式化为 JSON 字符串（3 处 lambda 共用）
std::string format_sub_agent_result(const agent::SubAgentResult& result) {
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
}

std::string format_sub_agent_error(const std::string& err) {
    return Json{
        {"success", false},
        {"error", err}
    }.dump();
}

} // anonymous namespace

void register_sub_agent_tools(
    capabilities::tool::ToolRegistry& registry,
    std::shared_ptr<agent::runtime::SubAgentRuntime> runtime) {

    registry.register_tool(
        std::string("delegate_task"),
        std::string("Offload a self-contained sub-task to a sub-agent. "
            "Use for: (1) noisy/dirty work — would clutter main context with irrelevant details "
            "(e.g., scrape a web page, search file contents, exploratory grep), "
            "(2) lengthy — contains large data the main agent doesn't need to see in full "
            "(e.g., batch execute commands, parse large logs, list directories), "
            "(3) heavy but narrow — can be done independently "
            "(e.g., format/translate content, batch process files). "
            "The sub-agent has basic file/command/network tools; no access to memory, "
            "planning, or workspace. "
            "Provide a self-contained prompt with ALL context needed. "
            "Returns summarized result — the main agent gets the essence, not the noise."),
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
                auto result = runtime->execute(task, config);
                return format_sub_agent_result(result);
            } catch (const std::exception& e) {
                return format_sub_agent_error(e.what());
            }
        });

    registry.register_tool(
        std::string("delegate_tasks"),
        std::string("Offload MULTIPLE independent sub-tasks to parallel sub-agents. "
            "Use when a task splits into pieces that are noisy, lengthy, or "
            "independent — each sub-agent handles one piece and returns a summarized "
            "result, keeping main context clean. "
            "Good for: searching across multiple files/directories, "
            "scraping multiple web pages, batch processing, "
            "researching several topics concurrently. "
            "Each sub-agent has basic file/command/network tools only. "
            "Provide a self-contained prompt per sub-agent. "
            "Returns array of summarized results."),
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
                    tasks, config, max_parallel);

                Json output = Json::array();
                for (const auto& r : results) {
                    output.push_back(Json::parse(format_sub_agent_result(r)));
                }
                return Json{{"results", output}, {"total", static_cast<int>(results.size())}}.dump();
            } catch (const std::exception& e) {
                return format_sub_agent_error(e.what());
            }
        });

    log::info_fmt("sub_agent_tools: registered delegate_task and delegate_tasks");
}

void register_custom_sub_agents(
    capabilities::tool::ToolRegistry& registry,
    std::shared_ptr<agent::runtime::SubAgentRuntime> runtime,
    const std::string& directory) {
    namespace fs = std::filesystem;

    fs::path dir(directory);
    if (!fs::is_directory(dir)) {
        log::warn_fmt("custom sub_agents dir not found: {}", directory);
        return;
    }

    int count = 0;
    for (const auto& entry : fs::directory_iterator(dir)) {
        if (!entry.is_regular_file()) continue;
        auto path = entry.path();
        if (path.extension() != ".md") continue;

        // 读取文件内容
        std::ifstream file(path, std::ios::binary);
        if (!file) continue;
        std::ostringstream oss;
        oss << file.rdbuf();
        std::string content = oss.str();

        // 解析 frontmatter（--- 分隔）
        auto fm_start = content.find("---");
        if (fm_start == std::string::npos) {
            log::warn_fmt("custom sub_agent: no frontmatter in {}", path.filename().string());
            continue;
        }
        auto fm_end = content.find("---", fm_start + 3);
        if (fm_end == std::string::npos) {
            log::warn_fmt("custom sub_agent: unmatched frontmatter in {}", path.filename().string());
            continue;
        }

        std::string frontmatter = content.substr(fm_start + 3, fm_end - fm_start - 3);
        std::string body = content.substr(fm_end + 3);
        // 去掉 body 首尾空白
        while (!body.empty() && (body.front() == '\n' || body.front() == '\r' || body.front() == ' '))
            body = body.substr(1);
        while (!body.empty() && (body.back() == '\n' || body.back() == '\r' || body.back() == ' '))
            body.pop_back();

        if (body.empty()) {
            log::warn_fmt("custom sub_agent: empty body in {}", path.filename().string());
            continue;
        }

        // 解析 frontmatter key: value
        std::string name, description, model, tools_str;
        int max_steps = 0;
        std::istringstream fm_stream(frontmatter);
        std::string line;
        while (std::getline(fm_stream, line)) {
            auto colon = line.find(':');
            if (colon == std::string::npos) continue;
            auto key = line.substr(0, colon);
            auto val = line.substr(colon + 1);
            // trim
            while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
            while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val = val.substr(1);
            while (!val.empty() && (val.back() == ' ' || val.back() == '\t' || val.back() == '\r')) val.pop_back();

            if (key == "name") name = std::string(val);
            else if (key == "description") description = std::string(val);
            else if (key == "model") model = std::string(val);
            else if (key == "tools") tools_str = std::string(val);
            else if (key == "max_steps") {
                try { max_steps = std::stoi(val); } catch (...) {}
            }
        }

        if (name.empty()) {
            log::warn_fmt("custom sub_agent: missing name in {}", path.filename().string());
            continue;
        }

        // 注册为 sub_<name> 工具
        std::string tool_name = "sub_" + name;
        std::string tool_desc = description.empty()
            ? std::string("Custom sub-agent: ") + name
            : description;

        // 解析工具白名单（逗号分隔的工具名列表）
        std::vector<std::string> tool_list;
        if (!tools_str.empty()) {
            std::istringstream ss(tools_str);
            std::string t;
            while (std::getline(ss, t, ',')) {
                while (!t.empty() && t.front() == ' ') t = t.substr(1);
                while (!t.empty() && t.back() == ' ') t.pop_back();
                if (!t.empty()) tool_list.push_back(t);
            }
        }

        registry.register_tool(
            tool_name,
            tool_desc,
            {
                {std::string("prompt"),
                 {std::string("string"),
                  std::string("The task prompt for the sub-agent"), {}, true}},
                {std::string("timeout"),
                 {std::string("integer"),
                  std::string("Timeout in milliseconds"), {}, false}},
            },
            [runtime, body, model, tool_list, max_steps](const Json& args) -> std::string {
                agent::SubAgentTask task;
                task.id = "custom_" + std::to_string(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count());
                task.prompt = args["prompt"].get<std::string>();
                task.system_prompt = body;
                task.tool_filter = tool_list;

                if (args.contains("timeout")) {
                    task.timeout = std::chrono::milliseconds(args["timeout"].get<int>());
                }

                // 如果 frontmatter 指定了 model，覆盖默认配置
                auto config = runtime->default_config();
                if (!model.empty()) {
                    config.model_override = model;
                }
                if (max_steps > 0) {
                    config.default_max_steps = max_steps;
                }

                try {
                    auto result = runtime->execute(task, config);
                    return format_sub_agent_result(result);
                } catch (const std::exception& e) {
                    return format_sub_agent_error(e.what());
                }
            });

        log::info_fmt("custom sub_agent registered: name={}, file={}", name, path.filename().string());
        count++;
    }

    log::info_fmt("custom sub_agents: loaded {} agent(s) from {}", count, directory);
}

// ═══════════════════════════════════════════════════════════════════
//  子 Agent 管理工具
// ═══════════════════════════════════════════════════════════════════

void register_sub_agent_management_tools(
    capabilities::tool::ToolRegistry& registry,
    std::shared_ptr<agent::runtime::SubAgentRuntime> runtime,
    const std::string& sub_agents_dir) {

    // ─── 1. sub_list ────────────────────────────────────────────
    registry.register_tool(
        std::string("subagent_list"),
        std::string("List all custom sub-agents available in ~/.bengear/sub_agents/."),
        {},
        [sub_agents_dir](const Json&) -> std::string {
            namespace fs = std::filesystem;
            fs::path dir(sub_agents_dir);
            if (!fs::is_directory(dir)) {
                return std::string(R"({"success":true,"sub_agents":[]})");
            }

            Json list = Json::array();
            for (const auto& entry : fs::directory_iterator(dir)) {
                if (!entry.is_regular_file() || entry.path().extension() != ".md") continue;
                Json item;
                item["name"] = entry.path().stem().string();
                item["file"] = entry.path().filename().string();
                list.push_back(std::move(item));
            }

            Json result;
            result["success"] = true;
            result["sub_agents"] = list;
            return result.dump();
        }
    );

    // ─── 2. sub_create ──────────────────────────────────────────
    registry.register_tool(
        std::string("subagent_create"),
        std::string("Create a custom sub-agent by specifying its name, description, "
            "and system prompt. The sub-agent will be available as sub_<name> tool."),
        {
            {std::string("name"), {
                std::string("string"),
                std::string("Sub-agent name (used as sub_<name> tool name)"),
            }},
            {std::string("description"), {
                std::string("string"),
                std::string("Brief description of what this sub-agent does"),
            }},
            {std::string("prompt"), {
                std::string("string"),
                std::string("System prompt / instructions for the sub-agent"),
            }},
            {std::string("model"), {
                std::string("string"),
                std::string("Optional model override"),
            }},
            {std::string("tools"), {
                std::string("string"),
                std::string("Optional comma-separated tool whitelist"),
            }},
        },
        [&registry, runtime, sub_agents_dir](const Json& args) -> std::string {
            auto name = args.value("name", std::string());
            auto description = args.value("description", std::string());
            auto prompt = args.value("prompt", std::string());

            if (name.empty() || prompt.empty()) {
                return std::string(R"({"success":false,"error":"name and prompt required"})");
            }

            namespace fs = std::filesystem;
            fs::create_directories(sub_agents_dir);

            auto md_path = fs::path(sub_agents_dir) / (name + ".md");
            if (fs::exists(md_path)) {
                return std::string(R"({"success":false,"error":"sub-agent already exists: )")
                    + name + "\"}";
            }

            {
                std::ofstream f(md_path);
                f << "---\n";
                f << "name: " << name << "\n";
                if (!description.empty()) f << "description: " << description << "\n";
                if (auto m = args.value("model", std::string()); !m.empty()) f << "model: " << m << "\n";
                if (auto t = args.value("tools", std::string()); !t.empty()) f << "tools: " << t << "\n";
                f << "---\n\n";
                f << prompt << "\n";
            }

            // 热加载：创建 .md 文件后立即重新扫描目录，
            // 新子 Agent 即刻生效，无需重启会话
            register_custom_sub_agents(registry, runtime, sub_agents_dir);

            Json result;
            result["success"] = true;
            result["name"] = name;
            result["tool_name"] = std::string("sub_") + name;
            result["message"] = "Sub-agent '" + name + "' created and ready to use.";
            return result.dump();
        }
    );

    // ─── 3. sub_remove ──────────────────────────────────────────
    registry.register_tool(
        std::string("subagent_remove"),
        std::string("Remove a custom sub-agent by deleting its .md file."),
        {
            {std::string("name"), {
                std::string("string"),
                std::string("Sub-agent name to remove"),
            }},
        },
        [&registry, runtime, sub_agents_dir](const Json& args) -> std::string {
            auto name = args.value("name", std::string());
            if (name.empty()) {
                return std::string(R"({"success":false,"error":"name required"})");
            }

            auto md_path = std::filesystem::path(sub_agents_dir) / (name + ".md");
            if (!std::filesystem::exists(md_path)) {
                return std::string(R"({"success":false,"error":"sub-agent not found: )")
                    + name + "\"}";
            }

            std::error_code ec;
            std::filesystem::remove(md_path, ec);
            if (ec) {
                return std::string(R"({"success":false,"error":"failed to delete file"})");
            }

            // 热加载：删除 .md 文件后重新扫描目录，移除对应工具
            register_custom_sub_agents(registry, runtime, sub_agents_dir);

            Json result;
            result["success"] = true;
            result["name"] = name;
            result["message"] = "Sub-agent '" + name + "' removed.";
            return result.dump();
        }
    );

    log::info_fmt("sub_agent management tools registered (dir: {})", sub_agents_dir);
}

} // namespace ben_gear::tools
