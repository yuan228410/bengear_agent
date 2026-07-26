#include "team/tools.hpp"
#include "team/orchestrator.hpp"

#include "log/logger.hpp"
#include "base/utils/json.hpp"
#include "platform/os.hpp"

#include <chrono>
#include <sstream>

namespace ben_gear::team {

void register_team_tools(
    capabilities::tool::ToolRegistry& registry,
    std::shared_ptr<TeamOrchestrator> orchestrator) {

    // ─── 1. create_team ──────────────────────────────────────────
    registry.register_tool(
        std::string("create_team"),
        std::string("Create or load a team from ~/.bengear/teams/{name}/. "
            "The team's agents have long-term memory and can collaborate. "
            "Use run_team to execute a workflow."),
        {
            {std::string("name"), {
                std::string("string"),
                std::string("Team name (directory under ~/.bengear/teams/)"),
            }},
        },
        [orchestrator](const Json& args) -> std::string {
            if (!orchestrator) {
                return std::string(R"({"success":false,"error":"team system not initialized"})");
            }

            auto name = args.value("name", std::string());
            if (name.empty()) {
                return std::string(R"({"success":false,"error":"name required"})");
            }

            // 默认 teams 目录
            auto teams_dir = std::filesystem::path(
                ben_gear::base::platform::os::data_directory()) / "teams";

            // 如果 teams_dir 下没有这个目录，尝试从全局配置加载
            if (!std::filesystem::is_directory(teams_dir / name)) {
                return std::string(R"({"success":false,"error":"team not found: )") + name + R"("})";
            }

            if (orchestrator->register_team(teams_dir, name)) {
                Json result;
                result["success"] = true;
                result["team_id"] = name;
                result["message"] = "Team '" + name + "' loaded. Use run_team to execute.";
                return result.dump();
            }

            return std::string(R"({"success":false,"error":"failed to load team: )") + name + R"("})";
        }
    );

    // ─── 2. run_team ────────────────────────────────────────────
    registry.register_tool(
        std::string("run_team"),
        std::string("Execute a team workflow. Agents collaborate based on "
            "the team's strategy (pipeline/sequential/parallel)."),
        {
            {std::string("team"), {
                std::string("string"),
                std::string("Team ID returned by create_team"),
            }},
            {std::string("objective"), {
                std::string("string"),
                std::string("The task objective for the team"),
            }},
        },
        [orchestrator](const Json& args) -> std::string {
            if (!orchestrator) {
                return std::string(R"({"success":false,"error":"team system not initialized"})");
            }

            auto team_id = args.value("team", std::string());
            auto objective = args.value("objective", std::string());

            if (team_id.empty() || objective.empty()) {
                return std::string(R"({"success":false,"error":"team and objective required"})");
            }

            auto exec_id = orchestrator->start(team_id, objective);
            if (exec_id.empty()) {
                return std::string(R"({"success":false,"error":"failed to start team workflow"})");
            }

            // 获取执行后的结果
            auto status = orchestrator->get_status(team_id);
            auto* ctx = orchestrator->context(team_id);

            Json result;
            result["success"] = true;
            result["execution_id"] = exec_id;
            if (status) {
                result["status"] = status->running ? "running" : "completed";
            }
            if (ctx) {
                auto snap = ctx->snapshot();
                Json artifacts = Json::array();
                for (const auto& [k, v] : snap.artifacts) {
                    Json item;
                    item["key"] = k;
                    // 只包含最后 500 字符作为预览
                    auto preview = v.substr(0, 500);
                    if (v.size() > 500) preview += "...";
                    item["preview"] = preview;
                    artifacts.push_back(std::move(item));
                }
                result["artifacts"] = artifacts;
            }

            return result.dump();
        }
    );

    // ─── 3. team_assign ──────────────────────────────────────────
    registry.register_tool(
        std::string("team_assign"),
        std::string("Assign a task to a specific team member."),
        {
            {std::string("team"), {
                std::string("string"),
                std::string("Team ID"),
            }},
            {std::string("member"), {
                std::string("string"),
                std::string("Member agent ID"),
            }},
            {std::string("task"), {
                std::string("string"),
                std::string("Task description"),
            }},
        },
        [orchestrator](const Json& args) -> std::string {
            if (!orchestrator) {
                return std::string(R"({"success":false,"error":"team system not initialized"})");
            }

            auto team_id = args.value("team", std::string());
            auto member = args.value("member", std::string());
            auto task = args.value("task", std::string());

            if (team_id.empty() || member.empty() || task.empty()) {
                return std::string(R"({"success":false,"error":"team, member, and task required"})");
            }

            if (orchestrator->dispatch(team_id, member, task)) {
                // 获取该成员的输出
                auto* ctx = orchestrator->context(team_id);
                Json result;
                result["success"] = true;
                if (ctx) {
                    auto output = ctx->read(member + "_output");
                    result["output"] = output.value_or("");
                }
                return result.dump();
            }

            return std::string(R"({"success":false,"error":"failed to assign task"})");
        }
    );

    // ─── 4. team_status ─────────────────────────────────────────
    registry.register_tool(
        std::string("team_status"),
        std::string("Get the current status of a team and its members."),
        {
            {std::string("team"), {
                std::string("string"),
                std::string("Team ID"),
            }},
        },
        [orchestrator](const Json& args) -> std::string {
            if (!orchestrator) {
                return std::string(R"({"success":false,"error":"team system not initialized"})");
            }

            auto team_id = args.value("team", std::string());
            if (team_id.empty()) {
                return std::string(R"({"success":false,"error":"team required"})");
            }

            auto status = orchestrator->get_status(team_id);
            if (!status) {
                return std::string(R"({"success":false,"error":"team not found: )") + team_id + R"("})";
            }

            Json result;
            result["success"] = true;
            result["team_id"] = status->team_id;
            result["running"] = status->running;
            result["current_stage"] = status->current_stage;

            Json members = Json::array();
            for (const auto& m : status->members) {
                Json member;
                member["agent_id"] = m.agent_id;
                member["name"] = m.name;
                const char* state_names[] = {"idle", "busy", "sleeping"};
                member["state"] = state_names[static_cast<int>(m.state)];
                member["has_error"] = m.has_error;
                members.push_back(std::move(member));
            }
            result["members"] = members;

            return result.dump();
        }
    );
}

} // namespace ben_gear::team
