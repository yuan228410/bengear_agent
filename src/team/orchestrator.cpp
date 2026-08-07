#include "team/orchestrator.hpp"
#include "team/loader.hpp"

#include "log/logger.hpp"
#include "agent/sub_agent_types.hpp"
#include "agent/core/events.hpp"
#include "base/utils/json.hpp"

#include <chrono>
#include <future>
#include <random>
#include <sstream>

namespace ben_gear::team {

namespace {

std::string generate_id() {
    // 使用 thread_local 单例避免每次构造 random_device + mt19937（性能热点）
    static thread_local std::mt19937_64 gen([] {
        std::random_device rd;
        // 用多个 random_device 值 + 时间戳混合种子，增强熵
        std::seed_seq seq{rd(), rd(), rd(),
            static_cast<unsigned>(std::chrono::steady_clock::now()
                .time_since_epoch().count())};
        return std::mt19937_64(seq);
    }());
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
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
    base::EventBus* event_bus,
    workspace::HistoryDB* history_db)
    : settings_(&settings)
    , provider_(&provider)
    , tools_(&tools)
    , event_bus_(event_bus)
    , history_db_(history_db) {}

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

    // 持久化执行记录到 SQLite
    if (history_db_) {
        persist_execution(*team, objective);
    }

    {
        std::unique_lock lock(mutex_);
        team->running = false;
    }
    return exec_id;
}

std::string TeamOrchestrator::start_with_plan(
    const std::string& team_id,
    const std::vector<PlanTaskItem>& items) {
    // 持锁：查找团队并设置运行状态
    std::shared_ptr<TeamInstance> team;
    {
        std::unique_lock lock(mutex_);
        team = unsafe_find(team_id);
        if (!team) return {};
        team->running = true;
        team->execution_id = generate_id();
    }

    log::info_fmt("team start_with_plan: {} items={} exec_id={}",
                  team_id, items.size(), team->execution_id);

    if (event_bus_) {
        event_bus_->publish(agent::TeamStartEvent{
            team_id, team->execution_id, "plan: " + std::to_string(items.size()) + " items"});
    }

    // 按计划项顺序执行，每个 item 分派给指定 Member（或按 members 顺序轮转）
    const auto& members = team->def.members;
    size_t member_idx = 0;
    std::string context;

    for (size_t i = 0; i < items.size(); ++i) {
        const auto& item = items[i];
        team->ctx.set_current_stage("plan_" + std::to_string(i + 1));

        // 确定执行者：指定了 assigned_to 就用它，否则按 members 顺序轮转
        std::string agent_id = item.assigned_to;
        if (agent_id.empty() && !members.empty()) {
            // 跳过 lead，从 member 开始轮转
            for (size_t offset = 0; offset < members.size(); ++offset) {
                size_t idx = (member_idx + offset) % members.size();
                if (members[idx].role == TeamRole::member) {
                    agent_id = members[idx].agent_id;
                    member_idx = idx + 1;
                    break;
                }
            }
        }
        if (agent_id.empty()) {
            log::error_fmt("team plan: item {} no available agent", i + 1);
            team->ctx.publish("plan_" + std::to_string(i + 1) + "_error",
                              "no available agent for: " + item.title);
            break;
        }

        // 构造任务 prompt：包含标题、描述和前置 context
        std::string task = "## Plan Item " + std::to_string(i + 1) + ": " + item.title;
        if (!item.description.empty()) {
            task += "\n" + item.description;
        }
        if (!context.empty()) {
            task += "\n\n## Previous Output\n" + context;
        }

        auto result = do_run_agent(*team, agent_id, task);

        if (result.success) {
            team->ctx.publish("plan_" + std::to_string(i + 1) + "_output", result.output);
            context = result.output;
        } else {
            team->ctx.publish("plan_" + std::to_string(i + 1) + "_error", result.error);
            log::error_fmt("team plan: item {} agent={} failed: {}",
                           i + 1, agent_id, result.error);
            break;  // 某项失败则中止
        }
    }

    // 持久化执行记录到 SQLite
    if (history_db_) {
        persist_execution(*team, "plan: " + std::to_string(items.size()) + " items");
    }

    {
        std::unique_lock lock(mutex_);
        team->running = false;
    }
    return team->execution_id;
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
                    if (event_bus_) {
                        event_bus_->publish(agent::TeamStageEvent{
                            team.def.team_id, team.execution_id,
                            stage.id, true, result.error});
                    }
                    return team.execution_id;
                }
                team.ctx.publish(stage.id + "_output", result.output);
            }
            // 发布 stage 完成事件（含最后一个 Agent 的输出）
            if (event_bus_) {
                auto last_output = team.ctx.read(stage.id + "_output");
                event_bus_->publish(agent::TeamStageEvent{
                    team.def.team_id, team.execution_id,
                    stage.id, true,
                    last_output.value_or("")});
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
        // 发布 stage 完成事件
        if (event_bus_) {
            event_bus_->publish(agent::TeamStageEvent{
                team.def.team_id, team.execution_id,
                member.agent_id, true, result.output});
        }
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

    // Phase 1: 确保所有 Agent 已创建，记录失败者
    std::vector<std::string> skipped;
    for (const auto& member : team.def.members) {
        if (!ensure_agent(team, member.agent_id)) {
            log::error_fmt("team parallel: failed to create agent {}", member.agent_id);
            skipped.push_back(member.agent_id);
        }
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
            team.ctx.publish(pt.agent_id + "_error", result.error);
        }
    };

    for (const auto& member : team.def.members) {
        // 跳过创建失败的 Agent
        if (std::find(skipped.begin(), skipped.end(), member.agent_id) != skipped.end()) {
            team.ctx.publish(member.agent_id + "_error",
                             "agent creation failed");
            continue;
        }

        // 窗口满时等待最早的 future 完成
        if (pending.size() >= max_parallel) {
            collect(pending.front());
            pending.erase(pending.begin());
        }

        // 构造 prompt：注入收件箱消息（与 do_run_agent 保持一致）
        // 注意：并行模式下无法在运行时收发消息（所有 Agent 同时启动），
        // 因此只注入执行前已存在的消息
        std::string full_prompt = objective;
        auto inbox = team.ctx.list_conversations(member.agent_id);
        if (!inbox.empty()) {
            full_prompt += "\n\n# Messages for you\n";
            for (const auto& msg : inbox) {
                full_prompt += "From " + msg.from + " (" + msg.subject + "):\n"
                             + msg.body + "\n---\n";
            }
            full_prompt += "\nUse team_send to reply if needed.";
        }

        auto& agent = *team.agents[member.agent_id];
        auto agent_id = member.agent_id;
        int timeout_s = agent.def().timeout_seconds > 0
                            ? agent.def().timeout_seconds : 120;
        pending.push_back({agent_id, std::async(std::launch::async,
            [&agent, full_prompt, agent_id, timeout_s]() {
            agent::SubAgentTask task;
            task.id = agent_id + "_task";
            task.prompt = full_prompt;
            task.system_prompt = agent.def().system_prompt;
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
//  安全团队操作（内部持锁，消除 context() 裸指针生命周期风险）
// ═══════════════════════════════════════════════════════════════════

bool TeamOrchestrator::send_team_message(
    const std::string& team_id,
    const std::string& from,
    const std::string& to,
    const std::string& subject,
    const std::string& body) {
    std::shared_lock lock(mutex_);
    auto team = unsafe_find(team_id);
    if (!team) return false;
    team->ctx.send_message(from, to, subject, body);
    return true;
}

std::string TeamOrchestrator::read_team_messages_json(
    const std::string& team_id,
    const std::string& member) {
    std::shared_lock lock(mutex_);
    auto team = unsafe_find(team_id);
    if (!team) return R"({"success":false,"error":"team not found"})";

    auto msgs = team->ctx.read_inbox(member);
    Json r;
    r["success"] = true;
    r["unread"] = static_cast<int64_t>(msgs.size());
    Json arr = Json::array();
    for (const auto& m : msgs) {
        Json j;
        j["from"] = m.from;
        j["subject"] = m.subject;
        j["body"] = m.body;
        arr.push_back(std::move(j));
    }
    r["messages"] = arr;
    return r.dump();
}

int TeamOrchestrator::broadcast_team_message(
    const std::string& team_id,
    const std::string& subject,
    const std::string& body) {
    std::shared_lock lock(mutex_);
    auto team = unsafe_find(team_id);
    if (!team) return 0;

    int sent = 0;
    for (const auto& m : team->def.members) {
        team->ctx.send_message("broadcast", m.agent_id, subject, body);
        ++sent;
    }
    return sent;
}

std::string TeamOrchestrator::team_snapshot_json(
    const std::string& team_id) const {
    std::shared_lock lock(mutex_);
    auto team = unsafe_find(team_id);
    if (!team) return R"({"success":false,"error":"team not found"})";

    auto snap = team->ctx.snapshot();
    Json r;
    r["success"] = true;
    Json arts = Json::array();
    std::string final_output;
    std::string last_error;
    for (const auto& [k, v] : snap.artifacts) {
        Json a;
        a["key"] = k;
        a["preview"] = v.substr(0, 500);
        arts.push_back(std::move(a));
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
    return r.dump();
}

std::string TeamOrchestrator::read_team_artifact(
    const std::string& team_id,
    const std::string& key) const {
    std::shared_lock lock(mutex_);
    auto team = unsafe_find(team_id);
    if (!team) return {};

    return team->ctx.read(key).value_or("");
}

// ═══════════════════════════════════════════════════════════════════
//  持久化
// ═══════════════════════════════════════════════════════════════════

void TeamOrchestrator::persist_execution(
    const TeamInstance& team, const std::string& objective) {
    if (!history_db_) return;

    // 构建执行记录 JSON
    Json record;
    record["team_id"] = team.def.team_id;
    record["execution_id"] = team.execution_id;
    record["objective"] = objective;
    record["strategy"] = [s = team.def.strategy] {
        switch (s) {
        case TeamStrategy::pipeline: return "pipeline";
        case TeamStrategy::sequential: return "sequential";
        case TeamStrategy::parallel: return "parallel";
        }
        return "unknown";
    }();

    // 收集黑板快照（artifacts + decisions）
    auto snap = team.ctx.snapshot();
    Json artifacts = Json::array();
    for (const auto& [k, v] : snap.artifacts) {
        Json a;
        a["key"] = k;
        a["value"] = v.substr(0, 2000);  // 截断过长的输出
        artifacts.push_back(std::move(a));
    }
    record["artifacts"] = artifacts;

    // 追加到团队历史（用 session_states KV 模式，key = "team:{team_id}"）
    auto session_key = "team:" + team.def.team_id;
    auto existing = history_db_->load_session_state(session_key, "team_history");

    Json history;
    if (!existing.empty()) {
        history = Json::parse(existing);
    }
    if (!history.is_array()) {
        history = Json::array();
    }
    history.push_back(record);

    // 限制历史记录数量，保留最近 50 条
    if (history.size() > 50) {
        Json trimmed = Json::array();
        for (size_t i = history.size() - 50; i < history.size(); ++i) {
            trimmed.push_back(history[i]);
        }
        history = std::move(trimmed);
    }

    // 异步持久化（fire-and-forget；HistoryDB 内部已处理错误日志）
    history_db_->save_session_state_async(session_key, "team_history", history.dump());
    log::info_fmt("team history persisted: {} exec_id={}", team.def.team_id, team.execution_id);
}

std::vector<Json> TeamOrchestrator::list_history(
    const std::string& team_id, int limit) const {
    if (!history_db_) return {};

    auto session_key = "team:" + team_id;
    auto existing = history_db_->load_session_state(session_key, "team_history");
    if (existing.empty()) return {};

    auto history = Json::parse(existing);
    if (!history.is_array()) return {};

    std::vector<Json> result;
    // 从最新的开始取
    int start = static_cast<int>(history.size()) - limit;
    if (start < 0) start = 0;
    for (int i = static_cast<int>(history.size()) - 1; i >= start; --i) {
        result.push_back(history[i]);
    }
    return result;
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

    // 注入收件箱消息到 prompt，然后清空收件箱防止重复注入
    auto inbox = team.ctx.list_conversations(agent_id);
    std::string full_task = task;
    if (!inbox.empty()) {
        full_task += "\n\n# Messages for you\n";
        for (const auto& msg : inbox) {
            full_task += "From " + msg.from + " (" + msg.subject + "):\n"
                       + msg.body + "\n---\n";
        }
        full_task += "\nUse team_send to reply if needed.";
        // 消息已注入到 prompt，清空收件箱防止长期运行团队 context 膨胀
        team.ctx.clear_inbox(agent_id);
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
