#include "server/api/agent_api.hpp"
#include "server/session/pool.hpp"
#include "agent/runtime/sub_agent_runtime.hpp"
#include "agent/runtime/sub_agent_tools.hpp"
#include "agent/builtin_agent.hpp"
#include "agent/sub_agent_types.hpp"
#include "team/orchestrator.hpp"
#include "team/types.hpp"
#include "platform/platform.hpp"
#include "log/logger.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace ben_gear::server {

namespace fs = std::filesystem;

void register_agent_routes(Router& router, SessionPool& session_pool) {

    // ── GET /api/agents — 返回所有可用 agent 列表 ──────────────
    router.add_route("GET", "/api/agents",
        [&session_pool](const HttpRequest& req) {
            Json agents_json = Json::array();

            // 内置 primary agent（build, plan 等）
            session_pool.for_each_active([&](const std::string&,
                                              const std::string&,
                                              const std::string&,
                                              SessionEntry& entry) {
                if (!entry.runtime) return;
                auto* builtin = entry.runtime->services()
                    .resolve<agent::BuiltinAgentRegistry>();
                if (!builtin) return;
                for (auto& a : builtin->agents()) {
                    Json ja;
                    ja["name"] = a.name;
                    ja["description"] = a.description.empty()
                        ? a.name : a.description;
                    ja["type"] = (a.category == agent::AgentCategory::primary)
                        ? "primary" : "builtin";
                    agents_json.push_back(std::move(ja));
                }
            });

            // 自定义 agent（通过共享的 list_custom_sub_agents 读取）
            std::string agents_dir = (fs::path(support::data_directory()) / "agents/sub").string();
            auto custom_agents = tools::list_custom_sub_agents(agents_dir);
            for (auto& ag : custom_agents) {
                Json a;
                a["name"] = ag.name;
                a["description"] = ag.description;
                a["type"] = "custom";
                agents_json.push_back(std::move(a));
            }

            // team agent — 从任意活跃会话的 ServiceRegistry 获取
            session_pool.for_each_active([&](const std::string&,
                                              const std::string&,
                                              const std::string&,
                                              SessionEntry& entry) {
                if (!entry.runtime) return;
                auto* orch = entry.runtime->services().resolve<team::TeamOrchestrator>();
                if (!orch) return;
                for (const auto& team_id : orch->list_teams()) {
                    auto team_def = orch->get_team(team_id);
                    if (!team_def) continue;
                    Json a;
                    a["name"] = team_def->name;
                    a["description"] = team_def->description;
                    a["type"] = "team";
                    a["team_id"] = team_id;
                    agents_json.push_back(std::move(a));
                }
            });

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

    log::info_fmt("API: agent routes registered (/api/agents, /api/agents/execute)");
}

} // namespace ben_gear::server
