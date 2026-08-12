#include "server/api/agent_api.hpp"
#include "server/session/pool.hpp"
#include "workspace/types.hpp"
#include "agent/runtime/sub_agent_runtime.hpp"
#include "agent/runtime/sub_agent_tools.hpp"
#include "agent/builtin_agent.hpp"
#include "agent/sub_agent_types.hpp"
#include "team/orchestrator.hpp"
#include "team/loader.hpp"
#include "team/types.hpp"
#include "platform/platform.hpp"
#include "capabilities/tool/registry.hpp"
#include "log/logger.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace ben_gear::server {

namespace fs = std::filesystem;

void register_agent_routes(Router& router, SessionPool& session_pool) {

    // ── GET /api/agents — 按 tier 返回 agent 列表 ──────────────
    router.add_route("GET", "/api/agents",
        [&session_pool](const HttpRequest& req) {
            Json agents_json = Json::array();

            // 解析 tier → 对应目录
            auto tier = std::string("global");
            auto it = req.query.find("tier");
            if (it != req.query.end()) tier = it->second;

            std::string base;
            if (tier == "global") {
                base = (fs::path(support::data_directory()) / "agents").string();
            } else if (tier == "workspace") {
                session_pool.for_each_active([&](const std::string&, const std::string&,
                                                  const std::string&, SessionEntry& entry) {
                    if (!entry.runtime || !base.empty()) return;
                    auto* ws = entry.runtime->services().resolve<workspace::WorkspaceContext>();
                    if (ws) base = (ws->tier_paths.workspace_dir / "agents").string();
                });
            }
            // user tier 暂未实现，base 为空 → 返回空列表
            if (base.empty()) return HttpResponse::ok("{\"agents\":[]}");

            // primary
            auto primary_agents = agent::BuiltinAgentRegistry::load_from_directory(
                (fs::path(base) / "primary").string());
            for (auto& a : primary_agents.agents()) {
                Json ja;
                ja["name"] = a.name;
                ja["description"] = a.description.empty() ? a.name : a.description;
                ja["type"] = "primary"; ja["tier"] = tier;
                agents_json.push_back(std::move(ja));
            }

            // sub
            auto custom_agents = tools::list_custom_sub_agents(
                (fs::path(base) / "sub").string());
            for (auto& ag : custom_agents) {
                Json a;
                a["name"] = ag.name; a["description"] = ag.description;
                a["type"] = "sub"; a["tier"] = tier;
                agents_json.push_back(std::move(a));
            }

            // team
            auto teams_dir = fs::path(base) / "team";
            auto team_ids = team::TeamLoader::list_teams(teams_dir);
            log::info_fmt("GET /api/agents: tier={} teams_dir={} team_count={}",
                tier, teams_dir.string(), team_ids.size());
            for (auto& tid : team_ids) {
                auto def = team::TeamLoader::load(teams_dir, tid);
                if (!def) {
                    log::warn_fmt("GET /api/agents: failed to load team {}", tid);
                    continue;
                }
                log::info_fmt("GET /api/agents: team={} members={} strategy={}",
                    tid, def->members.size(), static_cast<int>(def->strategy));
                Json a;
                a["name"] = def->name; a["description"] = def->description;
                a["type"] = "team"; a["team_id"] = tid; a["tier"] = tier;
                a["strategy"] = [&] {
                    switch (def->strategy) {
                        case team::TeamStrategy::pipeline: return "pipeline";
                        case team::TeamStrategy::sequential: return "sequential";
                        case team::TeamStrategy::parallel: return "parallel";
                    }
                    return "pipeline";
                }();
                Json members = Json::array();
                for (auto& m : def->members) {
                    Json jm;
                    jm["id"] = m.agent_id;
                    jm["name"] = m.name;
                    jm["role"] = (m.role == team::TeamRole::lead) ? "lead" : "member";
                    jm["description"] = m.description;
                    jm["prompt"] = m.system_prompt;  // .md body
                    jm["model"] = m.model_override;
                    jm["tools"] = [&] {
                        std::string ts;
                        for (size_t i = 0; i < m.tools.size(); ++i) {
                            if (i) ts += ",";
                            ts += m.tools[i];
                        }
                        return ts;
                    }();
                    members.push_back(std::move(jm));
                }
                a["members"] = members;
                agents_json.push_back(std::move(a));
            }

            Json resp;
            resp["agents"] = std::move(agents_json);
            auto dumped = resp.dump();
            return HttpResponse::ok(std::string(dumped.data(), dumped.size()));
        });

    // ── POST /api/agents/execute — 直接执行模式（旁路主 Agent）────────
    router.add_route("POST", "/api/agents/execute",
        [&session_pool](const HttpRequest& req) {
            // 解析请求体
            Json body;
            try {
                body = Json::parse(req.body);
            } catch (...) {
                return HttpResponse::error(400, "invalid JSON body");
            }

            auto agent_name = body.value("agent_name", "");
            auto prompt = body.value("prompt", "");
            auto system_prompt = body.value("system_prompt", "");
            if (agent_name.empty() || prompt.empty()) {
                return HttpResponse::error(400, "missing agent_name or prompt");
            }

            // 从活跃会话获取 SubAgentRuntime
            std::shared_ptr<agent::runtime::SubAgentRuntime> sub_agent;
            session_pool.for_each_active([&](const std::string&,
                                              const std::string&,
                                              const std::string&,
                                              SessionEntry& entry) {
                if (sub_agent) return;
                if (!entry.runtime) return;
                sub_agent = entry.runtime->services().resolve_shared<agent::runtime::SubAgentRuntime>();
            });

            if (!sub_agent) {
                return HttpResponse::error(503, "SubAgentRuntime 不可用，请先打开一个会话");
            }

            // 构造任务
            agent::SubAgentTask task;
            task.id = "mention_" + agent_name + "_" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count());
            task.prompt = prompt;
            task.system_prompt = system_prompt;
            task.timeout = sub_agent->default_config().default_timeout;

            // 同步执行（阻塞直到完成）
            auto result = sub_agent->execute(task, sub_agent->default_config());

            Json resp;
            resp["success"] = result.success;
            resp["output"] = result.output;
            resp["error"] = result.error;
            resp["tool_calls"] = result.tool_calls;
            resp["duration_ms"] = static_cast<int64_t>(result.duration.count());
            auto dumped = resp.dump();
            return HttpResponse::ok(std::string(dumped.data(), dumped.size()));
        });

    // ── POST /api/agents/create — 调工具直接创建 ──────────────
    router.add_route("POST", "/api/agents/create",
        [&session_pool](const HttpRequest& req) {
            Json body;
            try { body = Json::parse(req.body); }
            catch (...) { return HttpResponse::error(400, "invalid JSON"); }

            auto type = body.value("type", "sub");
            auto name = body.value("name", "");
            auto desc = body.value("description", "");
            auto prompt = body.value("prompt", "");
            if (name.empty()) return HttpResponse::error(400, "name required");

            // 从活跃会话获取 ToolRegistry
            capabilities::tool::ToolRegistry* tools = nullptr;
            session_pool.for_each_active([&](const std::string&, const std::string&,
                                              const std::string&, SessionEntry& entry) {
                if (tools) return;
                if (!entry.runtime) return;
                tools = &entry.runtime->services().resolve_ref<capabilities::tool::ToolRegistry>();
            });
            if (!tools) return HttpResponse::error(503, "no active session");

            // 选择工具
            std::string tool_name;
            if (type == "primary") tool_name = "agent_create";
            else if (type == "team") tool_name = "team_create";
            else tool_name = "subagent_create";

            Json args;
            args["name"] = name;
            if (!desc.empty()) args["description"] = desc;
            if (!prompt.empty()) args["prompt"] = prompt;
            if (type == "team") {
                args["strategy"] = body.value("strategy", "pipeline");
                args["members"] = body.value("members", Json::array());
            }
            if (body.contains("tier")) args["tier"] = body["tier"];
            if (type == "primary" && body.contains("mode")) args["mode"] = body["mode"];
            if (body.contains("tools")) args["tools"] = body["tools"];

            auto result = tools->execute(tool_name, args);
            Json resp = Json::parse(result.success ? result.output : result.error);
            auto dumped = resp.dump();
            return HttpResponse::ok(std::string(dumped.data(), dumped.size()));
        });

    // ── POST /api/agents/update — 更新 agent .md ──────────────
    router.add_route("POST", "/api/agents/update",
        [&session_pool](const HttpRequest& req) {
            Json body;
            try { body = Json::parse(req.body); }
            catch (...) { return HttpResponse::error(400, "invalid JSON"); }

            auto type = body.value("type", "sub");
            auto name = body.value("name", "");
            auto prompt = body.value("prompt", "");
            auto tier = body.value("tier", "workspace");
            if (name.empty()) return HttpResponse::error(400, "name required");

            // tier → 目录
            std::string base = (fs::path(support::data_directory()) / "agents").string();
            if (tier == "workspace") {
                session_pool.for_each_active([&](const std::string&, const std::string&,
                                                  const std::string&, SessionEntry& entry) {
                    if (!entry.runtime) return;
                    auto* ws = entry.runtime->services().resolve<workspace::WorkspaceContext>();
                    if (ws) base = (ws->tier_paths.workspace_dir / "agents").string();
                });
            }
            std::string sub = (type=="primary")?"primary":(type=="team")?"team":"sub";
            auto path = fs::path(base) / sub / (name + ".md");
            if (!fs::exists(path)) return HttpResponse::error(404, "agent not found");

            // 校验 frontmatter
            if (!prompt.empty()) {
                auto p1 = prompt.find("---");
                if (p1 == std::string::npos || prompt.find("---", p1+3) == std::string::npos)
                    return HttpResponse::error(400, "invalid frontmatter: missing --- delimiters");
            }

            if (prompt.empty()) {
                std::ifstream in(path);
                std::string old((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                auto p1 = old.find("---"), p2 = old.find("---", p1+3);
                prompt = ((p1!=std::string::npos&&p2!=std::string::npos)?old.substr(0,p2+3):"---\n---")
                       + "\n\n" + body.value("body", "") + "\n";
            }
            { std::ofstream out(path); out << prompt; if(prompt.back()!='\n') out<<'\n'; }

            Json r; r["success"]=true; r["message"]="Agent '"+name+"' updated.";
            return HttpResponse::ok(r.dump());
        });

    // ── POST /api/agents/delete — 删除 ──────────────────────
    router.add_route("POST", "/api/agents/delete",
        [&session_pool](const HttpRequest& req) {
            Json body;
            try { body = Json::parse(req.body); }
            catch (...) { return HttpResponse::error(400, "invalid JSON"); }
            auto type=body.value("type","sub"), name=body.value("name","");
            auto tier=body.value("tier","workspace");
            if(name.empty()) return HttpResponse::error(400,"name required");

            std::string base=(fs::path(support::data_directory())/"agents").string();
            if(tier=="workspace") session_pool.for_each_active([&](const std::string&,const std::string&,const std::string&,SessionEntry& e){
                if(!e.runtime)return; auto*w=e.runtime->services().resolve<workspace::WorkspaceContext>();
                if(w) base=(w->tier_paths.workspace_dir/"agents").string();
            });
            std::string sub=(type=="primary")?"primary":(type=="team")?"team":"sub";
            auto p=fs::path(base)/sub/(name+".md");
            if(!fs::exists(p)) return HttpResponse::error(404,"agent not found");
            std::error_code ec; fs::remove(p,ec);
            if(ec) return HttpResponse::error(500,"failed to delete");
            Json r; r["success"]=true; r["message"]="Agent '"+name+"' deleted.";
            return HttpResponse::ok(r.dump());
        });

    log::info_fmt("API: agent routes registered (/api/agents, /api/agents/create, update, delete, execute)");
}

} // namespace ben_gear::server
