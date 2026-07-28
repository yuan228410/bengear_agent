#include "team/tools.hpp"
#include "team/orchestrator.hpp"

#include "log/logger.hpp"
#include "base/utils/json.hpp"
#include "platform/os.hpp"

#include <chrono>
#include <fstream>
#include <sstream>

namespace ben_gear::team {

// 辅助：构建错误 JSON（用 Json 类确保特殊字符被正确转义）
static std::string err_json(const std::string& msg) {
    Json r;
    r["success"] = false;
    r["error"] = msg;
    return r.dump();
}

// 辅助：行级更新 frontmatter 字段（按 key 匹配整行替换，不存在则追加）
static void update_frontmatter_field(std::string& fm,
                                      const std::string& key,
                                      const std::string& val) {
    if (val.empty()) return;
    std::istringstream lines(fm);
    std::string line;
    std::string result;
    bool found = false;
    while (std::getline(lines, line)) {
        auto colon = line.find(':');
        if (colon != std::string::npos) {
            auto k = line.substr(0, colon);
            // 去掉 key 两端空白
            while (!k.empty() && k.back() == ' ') k.pop_back();
            while (!k.empty() && k.front() == ' ') k.erase(0, 1);
            if (k == key) {
                result += key + ": " + val + "\n";
                found = true;
                continue;
            }
        }
        result += line + "\n";
    }
    if (!found) result += key + ": " + val + "\n";
    fm = result;
}

void register_team_tools(
    capabilities::tool::ToolRegistry& registry,
    std::shared_ptr<TeamOrchestrator> orchestrator) {

    // ─── 1. create_team ──────────────────────────────────────────
    registry.register_tool(
        std::string("create_team"),
        std::string("Create or load a team from ~/.bengear/teams/{name}/. "
            "The team's agents have long-term memory and can collaborate. "
            "Use run_team to start the team."),
        {{std::string("name"), {std::string("string"),
          std::string("Team name (directory under ~/.bengear/teams/)")}}},
        [orchestrator](const Json& args) -> std::string {
            if (!orchestrator) return err_json("team system not initialized");
            auto name = args.value("name", std::string());
            if (name.empty()) return err_json("name required");
            auto teams_dir = std::filesystem::path(
                ben_gear::base::platform::os::data_directory()) / "teams";
            if (!std::filesystem::is_directory(teams_dir / name)) {
                return err_json("team not found: " + name);
            }
            if (orchestrator->register_team(teams_dir, name)) {
                Json r; r["success"] = true; r["team_id"] = name;
                r["message"] = "Team loaded. Use run_team to execute.";
                return r.dump();
            }
            return err_json("failed to load team: " + name);
        }
    );

    // ─── 2. run_team ────────────────────────────────────────────
    registry.register_tool(
        std::string("run_team"),
        std::string("Execute a team collaboration. Agents collaborate based on "
            "the team's strategy (pipeline/sequential/parallel). "
            "If plan_items is provided, each item is dispatched to a Member in order."),
        {{std::string("team"), {std::string("string"), std::string("Team ID")}},
         {std::string("objective"), {std::string("string"), std::string("Task objective")}},
         {std::string("plan_items"), {std::string("array"),
          std::string("Optional plan items: [{\"title\":\"...\",\"description\":\"...\",\"assigned_to\":\"agent_id\"}]")}}},
        [orchestrator](const Json& args) -> std::string {
            if (!orchestrator) return err_json("team system not initialized");
            auto team_id = args.value("team", std::string());
            if (team_id.empty()) return err_json("team required");

            std::string exec_id;
            if (args.contains("plan_items") && args["plan_items"].is_array()) {
                // 计划模式：按 plan_items 顺序分派
                std::vector<ben_gear::team::PlanTaskItem> items;
                for (const auto& item : args["plan_items"]) {
                    ben_gear::team::PlanTaskItem p;
                    p.title = item.value("title", std::string());
                    p.description = item.value("description", std::string());
                    p.assigned_to = item.value("assigned_to", std::string());
                    if (!p.title.empty()) items.push_back(std::move(p));
                }
                if (items.empty()) return err_json("plan_items is empty or invalid");
                exec_id = orchestrator->start_with_plan(team_id, items);
            } else {
                // 普通模式：按团队策略执行
                auto objective = args.value("objective", std::string());
                if (objective.empty()) return err_json("objective required");
                exec_id = orchestrator->start(team_id, objective);
            }

            if (exec_id.empty())
                return err_json("failed to start team");
            Json r; r["success"] = true; r["execution_id"] = exec_id;
            auto status = orchestrator->get_status(team_id);
            if (status) r["status"] = status->running ? "running" : "completed";
            auto* ctx = orchestrator->context(team_id);
            if (ctx) {
                auto snap = ctx->snapshot();
                Json arts = Json::array();
                std::string final_output;
                std::string last_error;
                for (const auto& [k, v] : snap.artifacts) {
                    Json a; a["key"] = k; a["preview"] = v.substr(0, 500);
                    arts.push_back(std::move(a));
                    // 取最后一个 _output 作为最终结果
                    if (k.size() > 7 && k.substr(k.size() - 7) == "_output") {
                        final_output = v;
                    }
                    if (k.size() > 6 && k.substr(k.size() - 6) == "_error") {
                        last_error = v;
                    }
                }
                r["artifacts"] = arts;
                if (!final_output.empty()) r["final_output"] = final_output;
                if (!last_error.empty()) r["last_error"] = last_error;
            }
            return r.dump();
        }
    );

    // ─── 3. team_assign ──────────────────────────────────────────
    registry.register_tool(
        std::string("team_assign"),
        std::string("Assign a task to a specific team member."),
        {{std::string("team"), {std::string("string"), std::string("Team ID")}},
         {std::string("member"), {std::string("string"), std::string("Member agent ID")}},
         {std::string("task"), {std::string("string"), std::string("Task description")}}},
        [orchestrator](const Json& args) -> std::string {
            if (!orchestrator) return err_json("team system not initialized");
            auto team_id = args.value("team", std::string());
            auto member = args.value("member", std::string());
            auto task = args.value("task", std::string());
            if (team_id.empty() || member.empty() || task.empty())
                return err_json("team, member, and task required");
            if (orchestrator->dispatch(team_id, member, task)) {
                Json r; r["success"] = true;
                auto* ctx = orchestrator->context(team_id);
                if (ctx) r["output"] = ctx->read(member + "_output").value_or("");
                return r.dump();
            }
            return err_json("failed to assign task");
        }
    );

    // ─── 4. team_status ─────────────────────────────────────────
    registry.register_tool(
        std::string("team_status"),
        std::string("Get the current status of a team and its members."),
        {{std::string("team"), {std::string("string"), std::string("Team ID")}}},
        [orchestrator](const Json& args) -> std::string {
            if (!orchestrator) return err_json("team system not initialized");
            auto team_id = args.value("team", std::string());
            if (team_id.empty()) return err_json("team required");
            auto status = orchestrator->get_status(team_id);
            if (!status) return err_json("team not found: " + team_id);
            Json r; r["success"] = true; r["team_id"] = team_id;
            r["running"] = status->running; r["current_stage"] = status->current_stage;
            Json members = Json::array();
            const char* states[] = {"idle", "busy", "sleeping"};
            for (const auto& m : status->members) {
                Json member; member["agent_id"] = m.agent_id; member["name"] = m.name;
                member["state"] = states[static_cast<int>(m.state)];
                member["has_error"] = m.has_error; members.push_back(std::move(member));
            }
            r["members"] = members;
            return r.dump();
        }
    );

    // ─── 5. team_list ───────────────────────────────────────────
    registry.register_tool(
        std::string("team_list"),
        std::string("List all loaded teams and their member states."),
        {},
        [orchestrator](const Json&) -> std::string {
            if (!orchestrator) return err_json("team system not initialized");
            Json list = Json::array();
            const char* states[] = {"idle", "busy", "sleeping"};
            for (const auto& tid : orchestrator->list_teams()) {
                Json t; t["team_id"] = tid;
                auto s = orchestrator->get_status(tid);
                if (s) {
                    t["running"] = s->running;
                    Json members = Json::array();
                    for (const auto& m : s->members) {
                        Json member; member["agent_id"] = m.agent_id; member["name"] = m.name;
                        member["state"] = states[static_cast<int>(m.state)];
                        members.push_back(std::move(member));
                    }
                    t["members"] = members;
                }
                list.push_back(std::move(t));
            }
            Json r; r["success"] = true; r["teams"] = list; return r.dump();
        }
    );

    // ─── 6. team_create ─────────────────────────────────────────
    registry.register_tool(
        std::string("team_create"),
        std::string("Create a new team by specifying members and roles. "
            "Creates .md files in ~/.bengear/teams/{name}/ and loads the team."),
        {{std::string("name"), {std::string("string"), std::string("Team name")}},
         {std::string("strategy"), {std::string("string"), std::string("pipeline/sequential/parallel")}},
         {std::string("members"), {std::string("array"), std::string("List of members. "
            "Each: id, name, role(lead/member), model(opt), tools(opt), description(opt)")}}},
        [orchestrator](const Json& args) -> std::string {
            if (!orchestrator) return err_json("team system not initialized");
            auto name = args.value("name", std::string());
            if (name.empty()) return err_json("name required");
            auto strategy = args.value("strategy", std::string("pipeline"));
            auto members = args.value("members", Json::array());
            if (!members.is_array() || members.empty())
                return err_json("at least one member required");
            auto dir = std::filesystem::path(ben_gear::base::platform::os::data_directory())
                       / "teams" / name;
            try { std::filesystem::create_directories(dir / "members"); }
            catch (...) { return err_json("cannot create directory"); }
            {
                std::ofstream f(dir / "team.md");
                f << "---\nname: " << name << "\ndescription: Team " << name
                  << "\nstrategy: " << strategy << "\n---\n\n# " << name << "\n";
            }
            int count = 0;
            for (const auto& m : members) {
                if (!m.is_object()) continue;
                auto id = m.value("id", std::string());
                if (id.empty()) continue;
                std::ofstream f(dir / "members" / (id + ".md"));
                f << "---\nname: " << id << "\ndisplay_name: " << m.value("name", id)
                  << "\nrole: " << m.value("role", "member") << "\n";
                if (auto v = m.value("model", std::string()); !v.empty()) f << "model: " << v << "\n";
                if (auto v = m.value("tools", std::string()); !v.empty()) f << "tools: " << v << "\n";
                if (auto v = m.value("description", std::string()); !v.empty()) f << "description: " << v << "\n";
                f << "---\n\nYou are " << m.value("name", id) << ".\n";
                ++count;
            }
            if (count == 0) return err_json("no valid members");
            if (orchestrator->register_team(dir.parent_path(), name)) {
                Json r; r["success"] = true; r["team_id"] = name;
                r["member_count"] = count; return r.dump();
            }
            return err_json("failed to register team");
        }
    );

    // ─── 7-13: 精简版工具 ──────────────────────────────────────

    // team_add_member
    registry.register_tool(std::string("team_add_member"),
        std::string("Add a new member to an existing team."),
        {{std::string("team"), {std::string("string"), std::string("Team ID")}},
         {std::string("id"), {std::string("string"), std::string("Member unique ID")}},
         {std::string("name"), {std::string("string"), std::string("Display name")}},
         {std::string("role"), {std::string("string"), std::string("lead or member")}},
         {std::string("model"), {std::string("string"), std::string("Optional model")}},
         {std::string("tools"), {std::string("string"), std::string("Optional tools")}}},
        [orchestrator](const Json& args) -> std::string {
            if (!orchestrator) return err_json("team system not initialized");
            auto team = args.value("team", std::string());
            auto id = args.value("id", std::string());
            if (team.empty() || id.empty()) return err_json("team and id required");
            auto dir = std::filesystem::path(ben_gear::base::platform::os::data_directory()) / "teams" / team;
            auto md = dir / "members" / (id + ".md");
            if (std::filesystem::exists(md)) return err_json("member already exists");
            try { std::filesystem::create_directories(dir / "members"); } catch (...) {}
            { std::ofstream f(md);
              f << "---\nname: " << id << "\ndisplay_name: " << args.value("name", id)
                << "\nrole: " << args.value("role", "member") << "\n";
              if (auto v = args.value("model", std::string()); !v.empty()) f << "model: " << v << "\n";
              if (auto v = args.value("tools", std::string()); !v.empty()) f << "tools: " << v << "\n";
              f << "---\n\nYou are " << args.value("name", id) << ".\n"; }
            if (orchestrator->register_team(dir.parent_path(), team)) {
                Json r; r["success"] = true; return r.dump();
            }
            return err_json("failed to reload team");
        }
    );

    // team_remove_member
    registry.register_tool(std::string("team_remove_member"),
        std::string("Remove a member from a team."),
        {{std::string("team"), {std::string("string"), std::string("Team ID")}},
         {std::string("member"), {std::string("string"), std::string("Member agent_id")}}},
        [orchestrator](const Json& args) -> std::string {
            if (!orchestrator) return err_json("team system not initialized");
            auto team = args.value("team", std::string());
            auto member = args.value("member", std::string());
            if (team.empty() || member.empty()) return err_json("team and member required");
            auto md = std::filesystem::path(ben_gear::base::platform::os::data_directory())
                      / "teams" / team / "members" / (member + ".md");
            if (!std::filesystem::exists(md)) return err_json("member not found");
            std::error_code ec; std::filesystem::remove(md, ec);
            if (ec) return err_json("failed to delete file");
            auto td = std::filesystem::path(ben_gear::base::platform::os::data_directory()) / "teams";
            if (orchestrator->register_team(td, team)) {
                Json r; r["success"] = true; return r.dump();
            }
            return err_json("file deleted but failed to reload team");
        }
    );

    // team_update_member
    registry.register_tool(std::string("team_update_member"),
        std::string("Update a team member configuration."),
        {{std::string("team"), {std::string("string"), std::string("Team ID")}},
         {std::string("member"), {std::string("string"), std::string("Member agent_id")}},
         {std::string("name"), {std::string("string"), std::string("New display name")}},
         {std::string("role"), {std::string("string"), std::string("New role")}},
         {std::string("model"), {std::string("string"), std::string("New model")}},
         {std::string("tools"), {std::string("string"), std::string("New tools")}}},
        [orchestrator](const Json& args) -> std::string {
            if (!orchestrator) return err_json("team system not initialized");
            auto team = args.value("team", std::string());
            auto member = args.value("member", std::string());
            if (team.empty() || member.empty()) return err_json("team and member required");
            auto md = std::filesystem::path(ben_gear::base::platform::os::data_directory())
                      / "teams" / team / "members" / (member + ".md");
            if (!std::filesystem::exists(md)) return err_json("member not found");
            std::ifstream in(md);
            std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            in.close();
            auto fs = content.find("---"); auto fe = content.find("---", fs + 3);
            if (fs == std::string::npos || fe == std::string::npos) return err_json("invalid file");
            std::string fm = content.substr(fs + 3, fe - fs - 3);
            std::string body = content.substr(fe + 3);

            update_frontmatter_field(fm, "display_name", args.value("name", std::string()));
            update_frontmatter_field(fm, "role", args.value("role", std::string()));
            update_frontmatter_field(fm, "model", args.value("model", std::string()));
            update_frontmatter_field(fm, "tools", args.value("tools", std::string()));
            { std::ofstream out(md); out << "---\n" << fm << "---" << body; }
            auto td = std::filesystem::path(ben_gear::base::platform::os::data_directory()) / "teams";
            if (orchestrator->register_team(td, team)) {
                Json r; r["success"] = true; return r.dump();
            }
            return err_json("failed to reload team");
        }
    );

    // team_update
    registry.register_tool(std::string("team_update"),
        std::string("Update team-level settings (strategy, description)."),
        {{std::string("team"), {std::string("string"), std::string("Team ID")}},
         {std::string("strategy"), {std::string("string"), std::string("New strategy")}},
         {std::string("description"), {std::string("string"), std::string("New description")}}},
        [orchestrator](const Json& args) -> std::string {
            if (!orchestrator) return err_json("team system not initialized");
            auto team = args.value("team", std::string());
            if (team.empty()) return err_json("team required");
            auto md = std::filesystem::path(ben_gear::base::platform::os::data_directory())
                      / "teams" / team / "team.md";
            if (!std::filesystem::exists(md)) return err_json("team not found");
            std::ifstream in(md);
            std::string c((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            in.close();
            auto fs = c.find("---"); auto fe = c.find("---", fs + 3);
            if (fs == std::string::npos || fe == std::string::npos) return err_json("invalid file");
            std::string fm = c.substr(fs + 3, fe - fs - 3);
            std::string body = c.substr(fe + 3);

            update_frontmatter_field(fm, "strategy", args.value("strategy", std::string()));
            update_frontmatter_field(fm, "description", args.value("description", std::string()));
            { std::ofstream out(md); out << "---\n" << fm << "---" << body; }
            auto td = std::filesystem::path(ben_gear::base::platform::os::data_directory()) / "teams";
            if (orchestrator->register_team(td, team)) {
                Json r; r["success"] = true; return r.dump();
            }
            return err_json("failed to reload team");
        }
    );

    // team_send
    registry.register_tool(std::string("team_send"),
        std::string("Send a message to a specific team member."),
        {{std::string("team"), {std::string("string"), std::string("Team ID")}},
         {std::string("to"), {std::string("string"), std::string("Recipient member ID")}},
         {std::string("subject"), {std::string("string"), std::string("Subject")}},
         {std::string("body"), {std::string("string"), std::string("Body")}},
         {std::string("from"), {std::string("string"), std::string("Sender ID, default 'user'")}}},
        [orchestrator](const Json& args) -> std::string {
            if (!orchestrator) return err_json("team system not initialized");
            auto team = args.value("team", std::string());
            auto to = args.value("to", std::string());
            if (team.empty() || to.empty()) return err_json("team and to required");
            auto* ctx = orchestrator->context(team);
            if (!ctx) return err_json("team not found");
            auto from = args.value("from", std::string("user"));
            ctx->send_message(from, to, args.value("subject", std::string()),
                              args.value("body", std::string()));
            Json r; r["success"] = true; return r.dump();
        }
    );

    // team_read_messages
    registry.register_tool(std::string("team_read_messages"),
        std::string("Read messages for a team member."),
        {{std::string("team"), {std::string("string"), std::string("Team ID")}},
         {std::string("member"), {std::string("string"), std::string("Member agent_id")}}},
        [orchestrator](const Json& args) -> std::string {
            if (!orchestrator) return err_json("team system not initialized");
            auto team = args.value("team", std::string());
            auto member = args.value("member", std::string());
            if (team.empty() || member.empty()) return err_json("team and member required");
            auto* ctx = orchestrator->context(team);
            if (!ctx) return err_json("team not found");
            auto msgs = ctx->read_inbox(member);
            Json r; r["success"] = true; r["unread"] = static_cast<int64_t>(msgs.size());
            Json arr = Json::array();
            for (const auto& m : msgs) {
                Json j; j["from"] = m.from; j["subject"] = m.subject; j["body"] = m.body;
                arr.push_back(std::move(j));
            }
            r["messages"] = arr; return r.dump();
        }
    );

    // team_broadcast
    registry.register_tool(std::string("team_broadcast"),
        std::string("Send a message to ALL members of a team."),
        {{std::string("team"), {std::string("string"), std::string("Team ID")}},
         {std::string("subject"), {std::string("string"), std::string("Subject")}},
         {std::string("body"), {std::string("string"), std::string("Body")}}},
        [orchestrator](const Json& args) -> std::string {
            if (!orchestrator) return err_json("team system not initialized");
            auto team = args.value("team", std::string());
            if (team.empty()) return err_json("team required");
            auto* ctx = orchestrator->context(team);
            if (!ctx) return err_json("team not found");
            auto def = orchestrator->get_team(team);
            if (!def) return err_json("team definition not found");
            int sent = 0;
            for (const auto& m : def->members) {
                ctx->send_message("broadcast", m.agent_id,
                    args.value("subject", std::string()),
                    args.value("body", std::string()));
                ++sent;
            }
            Json r; r["success"] = true; r["sent_to"] = sent; return r.dump();
        }
    );
}

} // namespace ben_gear::team
