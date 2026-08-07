#include "team/agent.hpp"

#include "memory/store.hpp"
#include "memory/context.hpp"
#include "log/logger.hpp"
#include "base/tier_paths.hpp"

#include <algorithm>
#include <filesystem>

namespace ben_gear::team {

namespace fs = std::filesystem;

PersistentAgent::PersistentAgent(
    AgentDef def,
    const capabilities::tool::ToolRegistry& shared_tools,
    const config::Settings& settings,
    llm::ProviderClient& provider,
    base::EventBus* event_bus)
    : def_(std::move(def))
    , shared_tools_(&shared_tools)
    , settings_(&settings)
    , provider_(&provider)
    , event_bus_(event_bus) {

    apply_tool_filter();
}

PersistentAgent::~PersistentAgent() {
    sleep();
}

void PersistentAgent::ensure_resources() {
    if (sub_rt_ && session_) return;

    fs::create_directories(def_.workspace / "memory");

    base::TierPaths tier_paths{
        def_.workspace, def_.workspace, def_.workspace
    };
    auto memory_store = std::make_shared<memory::MemoryStore>(tier_paths);
    ctx_builder_ = std::make_unique<memory::ContextBuilder>(
        *memory_store, std::string{});

    auto ws_name = std::string(def_.agent_id.data(), def_.agent_id.size());
    workspace::SessionDeps deps;
    deps.ws_ctx = workspace::WorkspaceContext{
        std::move(tier_paths), ws_name, def_.name, def_.agent_id
    };
    deps.memory_store = memory_store;
    deps.context_builder = ctx_builder_.get();

    auto empty_tools = std::make_shared<capabilities::tool::ToolRegistry>();
    workspace::SessionConfig session_cfg{
        def_.agent_id,
        settings_->llm.context_length,
        settings_->context_prune,
        config::SessionType::sub_agent,
        def_.name
    };

    session_ = std::make_unique<workspace::Session>(
        std::move(session_cfg), std::move(deps), *empty_tools);

    sub_rt_ = std::make_unique<agent::runtime::SubAgentRuntime>(
        *settings_, *provider_, *shared_tools_);
    if (event_bus_) sub_rt_->set_event_bus(event_bus_);

    log::info_fmt("team agent resources created: {} ({})",
                  def_.display_name, def_.agent_id);
}

void PersistentAgent::wakeup() {
    std::lock_guard lock(mutex_);
    if (sub_rt_ && session_) return;
    try {
        ensure_resources();
        state_.store(AgentLifecycle::idle);
    } catch (const std::exception& e) {
        log::error_fmt("team agent wakeup failed: {} - {}", def_.agent_id, e.what());
    }
}

agent::SubAgentResult PersistentAgent::execute(const agent::SubAgentTask& task) {
    // CAS 防止并发执行：idle → busy 原子转换
    AgentLifecycle expected = AgentLifecycle::idle;
    if (!state_.compare_exchange_strong(expected, AgentLifecycle::busy)) {
        agent::SubAgentResult err;
        err.success = false;
        err.error = std::string("agent busy: ") + def_.agent_id;
        return err;
    }

    // 持锁完成延迟初始化（ensure_resources 只在首次调用时执行）
    {
        std::unique_lock lock(mutex_);

        if (state_.load() == AgentLifecycle::sleeping) {
            // 不应该发生（sleeping 时 CAS 会失败），但做防御性处理
            state_.store(AgentLifecycle::busy);
        }

        if (!sub_rt_) {
            try {
                ensure_resources();
            } catch (const std::exception& e) {
                state_.store(AgentLifecycle::idle);
                agent::SubAgentResult err;
                err.success = false;
                err.error = std::string("agent wakeup failed: ") + e.what();
                return err;
            }
        }

        if (!sub_rt_) {
            state_.store(AgentLifecycle::idle);
            agent::SubAgentResult err;
            err.success = false;
            err.error = std::string("agent not initialized: ") + def_.agent_id;
            return err;
        }
    }
    // 锁已释放，实际执行不持锁 → status() / sleep() 不会被阻塞

    auto result = sub_rt_->execute(task, sub_config_);

    // 持锁更新会话历史
    {
        std::unique_lock lock(mutex_);
        if (session_ && result.success) {
            session_->history().add_user(task.prompt);
            session_->history().add_assistant(result.output);
        }
    }

    state_.store(AgentLifecycle::idle);
    return result;
}

void PersistentAgent::sleep() {
    std::lock_guard lock(mutex_);
    if (state_.load() == AgentLifecycle::sleeping) return;

    log::info_fmt("team agent sleeping: {} ({})",
                  def_.display_name, def_.agent_id);

    sub_rt_.reset();
    ctx_builder_.reset();
    session_.reset();
    state_.store(AgentLifecycle::sleeping);
}

PersistentAgent::StatusSummary PersistentAgent::status() const {
    std::lock_guard lock(mutex_);
    StatusSummary s;
    s.agent_id = def_.agent_id;
    s.name = def_.display_name;
    s.state = state_.load();
    s.session_count = 0;
    return s;
}

void PersistentAgent::apply_tool_filter() {
    sub_config_ = settings_->agent.sub_agent;

    if (def_.role == TeamRole::lead || def_.role == TeamRole::member) {
        auto& excluded = sub_config_.exclude_tools;
        auto remove_exclude = [&](const std::string& name) {
            auto it = std::find(excluded.begin(), excluded.end(), name);
            if (it != excluded.end()) excluded.erase(it);
        };

        remove_exclude("team_send");
        remove_exclude("team_read_messages");
        remove_exclude("team_list");
        remove_exclude("team_status");

        if (def_.role == TeamRole::lead) {
            remove_exclude("run_team");
            remove_exclude("team_assign");
            remove_exclude("team_broadcast");
        }
    }

    // 如果成员有自定义工具白名单，追加通信工具
    if (!def_.tools.empty()) {
        std::vector<std::string> allowed = def_.tools;
        auto ensure = [&](const std::string& t) {
            if (std::find(allowed.begin(), allowed.end(), t) == allowed.end())
                allowed.push_back(t);
        };
        if (def_.role == TeamRole::lead || def_.role == TeamRole::member) {
            ensure("team_send");
            ensure("team_read_messages");
            ensure("team_list");
            ensure("team_status");
            if (def_.role == TeamRole::lead) {
                ensure("run_team");
                ensure("team_assign");
                ensure("team_broadcast");
            }
        }
        sub_config_.tool_filter_default = allowed;
    }

    if (!def_.model_override.empty()) {
        sub_config_.model_override = def_.model_override;
    }
    if (def_.max_steps > 0) {
        sub_config_.default_max_steps = def_.max_steps;
    }
}

} // namespace ben_gear::team
