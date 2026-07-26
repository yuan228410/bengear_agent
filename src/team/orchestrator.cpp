#include "team/orchestrator.hpp"
#include "team/loader.hpp"

#include "log/logger.hpp"
#include "agent/sub_agent_types.hpp"
#include "agent/core/events.hpp"

#include <chrono>
#include <future>
#include <random>
#include <sstream>

namespace ben_gear::team {

namespace {

std::string generate_id() {
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::ostringstream oss;
    oss << "team_exec_" << ms << "_" << std::hex << gen();
    return oss.str();
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════
//  构造 / 析构
// ═══════════════════════════════════════════════════════════════════

TeamOrchestrator::TeamOrchestrator(
    const config::Settings& settings,
    llm::ProviderClient& provider,
    const capabilities::tool::ToolRegistry& tools,
    base::EventBus* event_bus)
    : settings_(&settings)
    , provider_(&provider)
    , tools_(&tools)
    , event_bus_(event_bus) {}

TeamOrchestrator::~TeamOrchestrator() {
    std::lock_guard lock(mutex_);
    for (auto& [_, team] : teams_) {
        for (auto& [ign, agent] : team->agents) {
            agent->sleep();
        }
    }
}

// ═══════════════════════════════════════════════════════════════════
//  团队注册 / 查询
// ═══════════════════════════════════════════════════════════════════

bool TeamOrchestrator::register_team(
    const std::filesystem::path& teams_dir,
    const std::string& team_id) {
    auto def = TeamLoader::load(teams_dir, team_id);
    if (!def) return false;

    std::lock_guard lock(mutex_);
    auto instance = std::make_unique<TeamInstance>();
    instance->def = std::move(*def);
    teams_[team_id] = std::move(instance);

    // 发布成员事件，让前端面板显示
    if (event_bus_) {
        for (const auto& m : teams_[team_id]->def.members) {
            event_bus_->publish(agent::TeamMemberEvent{
                team_id, std::string{}, m.agent_id,
                m.display_name, "idle", false, {}});
        }
    }

    log::info_fmt("team registered: {} ({}) members={}",
        team_id, teams_[team_id]->def.name, teams_[team_id]->def.members.size());
    return true;
}

std::vector<std::string> TeamOrchestrator::list_teams() const {
    std::lock_guard lock(mutex_);
    std::vector<std::string> result;
    result.reserve(teams_.size());
    for (const auto& [id, _] : teams_) result.push_back(id);
    return result;
}

std::optional<TeamDef> TeamOrchestrator::get_team(
    const std::string& team_id) const {
    std::lock_guard lock(mutex_);
    auto* team = unsafe_find(team_id);
    if (!team) return std::nullopt;
    return team->def;
}

// ═══════════════════════════════════════════════════════════════════
//  启动工作流
// ═══════════════════════════════════════════════════════════════════

std::string TeamOrchestrator::start(const std::string& team_id,
                                     const std::string& objective) {
    std::lock_guard lock(mutex_);
    auto* team = unsafe_find(team_id);
    if (!team) return {};

    team->running = true;
    team->execution_id = generate_id();
    log::info_fmt("team start: {} objective='{}' exec_id={}",
                  team_id, objective, team->execution_id);

    if (event_bus_) {
        event_bus_->publish(agent::TeamStartEvent{
            team_id, team->execution_id, objective});
    }

    auto exec_id = do_start(*team, objective);
    team->running = false;
    return exec_id;
}

std::string TeamOrchestrator::do_start(
    TeamInstance& team, const std::string& objective) {
    switch (team.def.strategy) {
    case TeamStrategy::pipeline:
        return do_pipeline(team, objective);
    case TeamStrategy::sequential:
        return do_sequential(team, objective);
    case TeamStrategy::parallel:
        return do_parallel(team, objective);
    }
    return team.execution_id;
}

// ═══════════════════════════════════════════════════════════════════
//  Pipeline 策略
// ═══════════════════════════════════════════════════════════════════

std::string TeamOrchestrator::do_pipeline(
    TeamInstance& team, const std::string& objective) {

    if (!team.def.stages.empty()) {
        for (const auto& stage : team.def.stages) {
            team.ctx.set_current_stage(stage.id);
            if (event_bus_) {
                event_bus_->publish(agent::TeamStageEvent{
                    team.def.team_id, team.execution_id,
                    stage.id, false, stage.description});
            }

            std::string context = objective;
            for (const auto& dep : stage.depends_on) {
                auto dep_output = team.ctx.read(dep + "_output");
                if (dep_output) {
                    context += "\n\n## " + dep + " output\n" + *dep_output;
                }
            }
            for (const auto& agent_id : stage.assigned_agents) {
                auto result = do_run_agent(team, agent_id, context);
                if (!result.success) {
                    log::error_fmt("team pipeline: stage={} agent={} failed: {}",
                                   stage.id, agent_id, result.error);
                    return team.execution_id;
                }
                team.ctx.publish(stage.id + "_output", result.output);
            }
        }
        return team.execution_id;
    }

    std::string context = objective;
    for (const auto& member : team.def.members) {
        team.ctx.set_current_stage(member.agent_id);
        if (event_bus_) {
            event_bus_->publish(agent::TeamStageEvent{
                team.def.team_id, team.execution_id,
                member.agent_id, false,
                "Executing: " + member.display_name});
        }
        auto result = do_run_agent(team, member.agent_id, context);
        if (!result.success) {
            log::error_fmt("team pipeline: agent={} failed: {}",
                           member.agent_id, result.error);
            return team.execution_id;
        }
        team.ctx.publish(member.agent_id + "_output", result.output);
        context = result.output;
    }
    return team.execution_id;
}

// ═══════════════════════════════════════════════════════════════════
//  Sequential / Parallel
// ═══════════════════════════════════════════════════════════════════

std::string TeamOrchestrator::do_sequential(
    TeamInstance& team, const std::string& objective) {
    std::string context = objective;
    for (const auto& member : team.def.members) {
        team.ctx.set_current_stage(member.agent_id);
        auto result = do_run_agent(team, member.agent_id, context);
        if (!result.success) {
            log::error_fmt("team sequential: agent={} failed: {}",
                           member.agent_id, result.error);
            return team.execution_id;
        }
        team.ctx.publish(member.agent_id + "_output", result.output);
        context = result.output;
    }
    return team.execution_id;
}

std::string TeamOrchestrator::do_parallel(
    TeamInstance& team, const std::string& objective) {
    team.ctx.set_current_stage("parallel");

    // Phase 1: 确保所有 Agent 已创建（持有锁时完成）
    for (const auto& member : team.def.members) {
        ensure_agent(team, member.agent_id);
    }

    // Phase 2: 并行执行
    std::vector<std::future<agent::SubAgentResult>> futures;
    for (const auto& member : team.def.members) {
        auto prompt = std::string("As ") + member.display_name + ":\n" + objective;
        auto& agent = *team.agents[member.agent_id];
        futures.push_back(std::async(std::launch::async, [&agent, prompt]() {
            agent::SubAgentTask task;
            task.id = agent.def().agent_id + "_task";
            task.prompt = prompt;
            task.timeout = std::chrono::milliseconds(120000);
            return agent.execute(task);
        }));
    }

    for (size_t i = 0; i < futures.size(); ++i) {
        auto result = futures[i].get();
        if (result.success) {
            team.ctx.publish(team.def.members[i].agent_id + "_output", result.output);
        }
    }

    return team.execution_id;
}

// ═══════════════════════════════════════════════════════════════════
//  派发 / 状态 / 休眠
// ═══════════════════════════════════════════════════════════════════

bool TeamOrchestrator::dispatch(const std::string& team_id,
                                 const std::string& agent_id,
                                 const std::string& task) {
    std::lock_guard lock(mutex_);
    auto* team = unsafe_find(team_id);
    if (!team) return false;

    auto result = do_run_agent(*team, agent_id, task);
    if (result.success) {
        team->ctx.publish(agent_id + "_output", result.output);
    }
    return result.success;
}

std::optional<TeamStatus> TeamOrchestrator::get_status(
    const std::string& team_id) const {
    std::lock_guard lock(mutex_);
    auto* team = unsafe_find(team_id);
    if (!team) return std::nullopt;

    TeamStatus status;
    status.team_id = team_id;
    status.execution_id = team->execution_id;
    status.running = team->running;
    status.current_stage = team->ctx.current_stage();

    for (const auto& [id, agent] : team->agents) {
        TeamStatus::MemberStatus ms;
        ms.agent_id = id;
        ms.name = agent->def().display_name;
        ms.state = agent->state();
        status.members.push_back(std::move(ms));
    }
    return status;
}

TeamContext* TeamOrchestrator::context(const std::string& team_id) {
    std::lock_guard lock(mutex_);
    auto* team = unsafe_find(team_id);
    return team ? &team->ctx : nullptr;
}

bool TeamOrchestrator::sleep_team(const std::string& team_id) {
    std::lock_guard lock(mutex_);
    auto* team = unsafe_find(team_id);
    if (!team) return false;
    for (auto& [_, agent] : team->agents) {
        agent->sleep();
    }
    return true;
}

// ═══════════════════════════════════════════════════════════════════
//  内部
// ═══════════════════════════════════════════════════════════════════

TeamOrchestrator::TeamInstance* TeamOrchestrator::unsafe_find(
    const std::string& team_id) {
    auto it = teams_.find(team_id);
    return (it != teams_.end()) ? it->second.get() : nullptr;
}

const TeamOrchestrator::TeamInstance* TeamOrchestrator::unsafe_find(
    const std::string& team_id) const {
    auto it = teams_.find(team_id);
    return (it != teams_.end()) ? it->second.get() : nullptr;
}

bool TeamOrchestrator::ensure_agent(
    TeamInstance& team, const std::string& agent_id) {
    if (team.agents.find(agent_id) != team.agents.end()) return true;

    const AgentDef* def = nullptr;
    for (const auto& m : team.def.members) {
        if (m.agent_id == agent_id) { def = &m; break; }
    }
    if (!def) return false;

    auto agent = std::make_unique<PersistentAgent>(
        *def, *tools_, *settings_, *provider_, event_bus_);
    agent->wakeup();
    if (agent->state() == AgentLifecycle::sleeping) {
        return false;  // 唤醒失败
    }
    team.agents.emplace(agent_id, std::move(agent));
    return true;
}

agent::SubAgentResult TeamOrchestrator::do_run_agent(
    TeamInstance& team,
    const std::string& agent_id,
    const std::string& task) {

    if (!ensure_agent(team, agent_id)) {
        agent::SubAgentResult err;
        err.success = false;
        err.error = std::string("agent not found: ") + agent_id;
        return err;
    }

    auto& agent = *team.agents[agent_id];
    if (agent.state() == AgentLifecycle::sleeping) {
        agent.wakeup();
    }

    // 发布 MemberEvent（工作中）
    if (event_bus_) {
        event_bus_->publish(agent::TeamMemberEvent{
            team.def.team_id, team.execution_id, agent_id,
            agent.def().display_name, "busy", false, {}});
    }

    // 注入收件箱消息到 prompt
    auto inbox = team.ctx.list_conversations(agent_id);
    std::string full_task = task;
    if (!inbox.empty()) {
        full_task += "\n\n# Messages for you\n";
        for (const auto& msg : inbox) {
            full_task += "From " + msg.from + " (" + msg.subject + "):\n"
                       + msg.body + "\n---\n";
        }
        full_task += "\nUse team_send to reply if needed.";
    }

    agent::SubAgentTask agent_task;
    agent_task.id = agent_id + "_" + generate_id();
    agent_task.prompt = full_task;
    agent_task.timeout = std::chrono::milliseconds(120000);

    auto result = agent.execute(agent_task);

    // 发布 Agent 输出事件
    if (event_bus_) {
        event_bus_->publish(agent::TeamMemberOutputEvent{
            team.def.team_id, team.execution_id, agent_id,
            agent.def().display_name, result.output, true});
    }

    if (event_bus_) {
        event_bus_->publish(agent::TeamMemberEvent{
            team.def.team_id, team.execution_id, agent_id,
            agent.def().display_name,
            result.success ? "idle" : "idle",
            !result.success, result.error});
    }

    return result;
}

} // namespace ben_gear::team
