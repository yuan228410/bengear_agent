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
    std::unique_lock lock(mutex_);
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

    std::unique_lock lock(mutex_);

    // 团队正在运行时拒绝覆盖，避免销毁运行中的 Agent 和上下文
    auto it = teams_.find(team_id);
    if (it != teams_.end() && it->second->running) {
        log::error_fmt("team register failed: {} is running, cannot reload",
                       team_id);
        return false;
    }

    auto instance = std::make_shared<TeamInstance>();
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
    std::shared_lock lock(mutex_);
    std::vector<std::string> result;
    result.reserve(teams_.size());
    for (const auto& [id, _] : teams_) result.push_back(id);
    return result;
}

std::optional<TeamDef> TeamOrchestrator::get_team(
    const std::string& team_id) const {
    std::shared_lock lock(mutex_);
    auto team = unsafe_find(team_id);
    if (!team) return std::nullopt;
    return team->def;
}

// ═══════════════════════════════════════════════════════════════════
//  启动工作流
// ═══════════════════════════════════════════════════════════════════

std::string TeamOrchestrator::start(const std::string& team_id,
                                     const std::string& objective) {
    // 持锁：查找团队并设置运行状态
    std::shared_ptr<TeamInstance> team;
    {
        std::unique_lock lock(mutex_);
        team = unsafe_find(team_id);
        if (!team) return {};
        team->running = true;
        team->execution_id = generate_id();
    }

    log::info_fmt("team start: {} objective='{}' exec_id={}",
                  team_id, objective, team->execution_id);

    if (event_bus_) {
        event_bus_->publish(agent::TeamStartEvent{
            team_id, team->execution_id, objective});
    }

    // 执行期间不持锁，允许 get_status / list_teams 等读操作并发
    auto exec_id = do_start(*team, objective);

    {
        std::unique_lock lock(mutex_);
        team->running = false;
    }
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
                    // 发布错误信息，保留已完成 stage 的输出
                    team.ctx.publish(stage.id + "_error", result.error);
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
            team.ctx.publish(member.agent_id + "_error", result.error);
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
            team.ctx.publish(member.agent_id + "_error", result.error);
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

    // Phase 1: 确保所有 Agent 已创建
    for (const auto& member : team.def.members) {
        ensure_agent(team, member.agent_id);
    }

    // Phase 2: 并行执行（限制并发数为 max_concurrent）
    const size_t max_parallel = static_cast<size_t>(
        team.def.max_concurrent > 0 ? team.def.max_concurrent : 3);

    struct PendingTask {
        std::string agent_id;
        std::future<agent::SubAgentResult> future;
    };
    std::vector<PendingTask> pending;
    pending.reserve(max_parallel);

    auto collect = [&](PendingTask& pt) {
        auto result = pt.future.get();
        if (result.success) {
            team.ctx.publish(pt.agent_id + "_output", result.output);
        } else {
            // 失败也发布，让调用者知道哪个 Agent 出错
            team.ctx.publish(pt.agent_id + "_error", result.error);
        }
    };

    for (const auto& member : team.def.members) {
        // 窗口满时等待最早的 future 完成
        if (pending.size() >= max_parallel) {
            collect(pending.front());
            pending.erase(pending.begin());
        }

        auto prompt = std::string("As ") + member.display_name + ":\n" + objective;
        auto& agent = *team.agents[member.agent_id];
        auto system_prompt = agent.def().system_prompt;
        auto agent_id = member.agent_id;
        // 超时：agent 配置优先，否则默认 120s
        int timeout_s = agent.def().timeout_seconds > 0
                            ? agent.def().timeout_seconds : 120;
        pending.push_back({agent_id, std::async(std::launch::async,
            [&agent, prompt, system_prompt, agent_id, timeout_s]() {
            agent::SubAgentTask task;
            task.id = agent_id + "_task";
            task.prompt = prompt;
            task.system_prompt = system_prompt;
            task.timeout = std::chrono::milliseconds(timeout_s * 1000);
            return agent.execute(task);
        })});
    }

    // 收集剩余结果
    for (auto& pt : pending) {
        collect(pt);
    }

    return team.execution_id;
}

// ═══════════════════════════════════════════════════════════════════
//  派发 / 状态 / 休眠
// ═══════════════════════════════════════════════════════════════════

bool TeamOrchestrator::dispatch(const std::string& team_id,
                                 const std::string& agent_id,
                                 const std::string& task) {
    // 持锁取出 shared_ptr，释放后执行
    std::shared_ptr<TeamInstance> team;
    {
        std::shared_lock lock(mutex_);
        team = unsafe_find(team_id);
        if (!team) return false;
    }

    auto result = do_run_agent(*team, agent_id, task);
    if (result.success) {
        team->ctx.publish(agent_id + "_output", result.output);
    }
    return result.success;
}

std::optional<TeamStatus> TeamOrchestrator::get_status(
    const std::string& team_id) const {
    std::shared_lock lock(mutex_);
    auto team = unsafe_find(team_id);
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
    std::shared_lock lock(mutex_);
    auto team = unsafe_find(team_id);
    return team ? &team->ctx : nullptr;
}

bool TeamOrchestrator::sleep_team(const std::string& team_id) {
    std::unique_lock lock(mutex_);
    auto team = unsafe_find(team_id);
    if (!team) return false;
    for (auto& [_, agent] : team->agents) {
        agent->sleep();
    }
    return true;
}

// ═══════════════════════════════════════════════════════════════════
//  内部
// ═══════════════════════════════════════════════════════════════════

std::shared_ptr<TeamOrchestrator::TeamInstance> TeamOrchestrator::unsafe_find(
    const std::string& team_id) {
    auto it = teams_.find(team_id);
    return (it != teams_.end()) ? it->second : nullptr;
}

std::shared_ptr<const TeamOrchestrator::TeamInstance> TeamOrchestrator::unsafe_find(
    const std::string& team_id) const {
    auto it = teams_.find(team_id);
    return (it != teams_.end()) ? it->second : nullptr;
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
    agent_task.system_prompt = agent.def().system_prompt;
    // 超时：agent 配置优先，否则默认 120s
    int timeout_s = agent.def().timeout_seconds > 0
                        ? agent.def().timeout_seconds : 120;
    agent_task.timeout = std::chrono::milliseconds(timeout_s * 1000);

    // 执行（支持重试）
    int max_retries = agent.def().max_retries;
    agent::SubAgentResult result;
    for (int attempt = 0; attempt <= max_retries; ++attempt) {
        result = agent.execute(agent_task);
        if (result.success) break;
        if (attempt < max_retries) {
            log::warn_fmt("team agent {} attempt {}/{} failed, retrying: {}",
                          agent_id, attempt + 1, max_retries, result.error);
        }
    }

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
            result.success ? "idle" : "error",
            !result.success, result.error});
    }

    return result;
}

} // namespace ben_gear::team
