#include "team/orchestrator.hpp"
#include "team/loader.hpp"

#include "log/logger.hpp"
#include "agent/sub_agent_types.hpp"

#include <chrono>
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
        for (auto& [_, agent] : team->agents) {
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
    log::info_fmt("team registered: {} ({})", team_id, teams_[team_id]->def.name);
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
        // 按 stages 定义执行
        for (const auto& stage : team.def.stages) {
            team.ctx.set_current_stage(stage.id);

            // 收集上游 artifacts 作为上下文
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
                // 后续 stage 可从黑板读取
            }
        }
        return team.execution_id;
    }

    // 无 stages 定义，按成员顺序执行
    std::string context = objective;
    for (const auto& member : team.def.members) {
        team.ctx.set_current_stage(member.agent_id);
        auto result = do_run_agent(team, member.agent_id, context);
        if (!result.success) {
            log::error_fmt("team pipeline: agent={} failed: {}",
                           member.agent_id, result.error);
            return team.execution_id;
        }
        team.ctx.publish(member.agent_id + "_output", result.output);
        context = result.output;  // 下游 Agent 看到上游的输出
    }
    return team.execution_id;
}

// ═══════════════════════════════════════════════════════════════════
//  Sequential / Parallel 策略
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
    for (const auto& member : team.def.members) {
        auto prompt = std::string("As ") + member.display_name + ":\n" + objective;
        auto result = do_run_agent(team, member.agent_id, prompt);
        if (result.success) {
            team.ctx.publish(member.agent_id + "_output", result.output);
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
//  内部：查找（调用者必须持有锁）
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

// ═══════════════════════════════════════════════════════════════════
//  内部：执行单个 Agent
// ═══════════════════════════════════════════════════════════════════

agent::SubAgentResult TeamOrchestrator::do_run_agent(
    TeamInstance& team,
    const std::string& agent_id,
    const std::string& task) {

    // 查找或创建 Agent（lazy init）
    auto agent_it = team.agents.find(agent_id);
    if (agent_it == team.agents.end()) {
        // 从 def 中查找对应成员
        const AgentDef* def = nullptr;
        for (const auto& m : team.def.members) {
            if (m.agent_id == agent_id) { def = &m; break; }
        }
        if (!def) {
            agent::SubAgentResult err;
            err.success = false;
            err.error = std::string("agent not found: ") + agent_id;
            return err;
        }

        auto agent = std::make_unique<PersistentAgent>(
            *def, *tools_, *settings_, *provider_, event_bus_);
        agent->wakeup();
        agent_it = team.agents.emplace(agent_id, std::move(agent)).first;
    }

    auto& agent = *agent_it->second;
    if (agent.state() == AgentLifecycle::sleeping) {
        agent.wakeup();
    }

    agent::SubAgentTask agent_task;
    agent_task.id = agent_id + "_" + generate_id();
    agent_task.prompt = task;
    agent_task.timeout = std::chrono::milliseconds(120000);

    return agent.execute(agent_task);
}

} // namespace ben_gear::team
