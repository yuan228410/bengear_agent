#include "team/agent.hpp"

#include "memory/store.hpp"
#include "memory/context.hpp"
#include "log/logger.hpp"
#include "base/tier_paths.hpp"

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

void PersistentAgent::wakeup() {
    std::lock_guard lock(mutex_);
    if (state_.load() != AgentLifecycle::sleeping && session_) {
        return;  // 已经活跃
    }

    try {
        // 确保工作目录存在
        fs::create_directories(def_.workspace / "memory");

        // 创建 Agent 专属的 MemoryStore（使用 agent 工作区作为 tier）
        base::TierPaths tier_paths{
            def_.workspace,      // global
            def_.workspace,      // user
            def_.workspace       // workspace
        };
        auto memory_store = std::make_shared<memory::MemoryStore>(tier_paths);

        // 创建 ContextBuilder
        auto builder = std::make_unique<memory::ContextBuilder>(
            *memory_store, std::string{});

        // SessionDeps：Agent 专属的 workspace 上下文
        auto ws_name = std::string(def_.agent_id.data(), def_.agent_id.size());
        workspace::SessionDeps deps;
        deps.ws_ctx = workspace::WorkspaceContext{
            std::move(tier_paths),
            ws_name,
            def_.name,
            def_.agent_id
        };
        deps.memory_store = memory_store;
        deps.context_builder = builder.get();

        // Session 构造需要一个 ToolRegistry 引用
        // Agent 不使用 Session 内置的工具注册（走 SubAgentRuntime 的过滤工具）
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

        // 创建 SubAgentRuntime（执行引擎）
        sub_rt_ = std::make_unique<agent::runtime::SubAgentRuntime>(
            *settings_, *provider_, *shared_tools_);

        if (event_bus_) {
            sub_rt_->set_event_bus(event_bus_);
        }

        state_.store(AgentLifecycle::idle);
        log::info_fmt("team agent woken up: {} ({})", def_.display_name, def_.agent_id);

    } catch (const std::exception& e) {
        log::error_fmt("team agent wakeup failed: {} - {}", def_.agent_id, e.what());
        state_.store(AgentLifecycle::idle);
    }
}

agent::SubAgentResult PersistentAgent::execute(const agent::SubAgentTask& task) {
    std::unique_lock lock(mutex_);

    if (state_.load() == AgentLifecycle::sleeping) {
        lock.unlock();
        wakeup();
        lock.lock();
    }

    if (!sub_rt_) {
        agent::SubAgentResult err;
        err.success = false;
        err.error = std::string("agent not initialized: ") + def_.agent_id;
        return err;
    }

    state_.store(AgentLifecycle::busy);

    // 执行任务（SubAgentRuntime 内部管理自己的 history）
    auto result = sub_rt_->execute(task, sub_config_);

    // 记录到 session
    if (session_ && result.success) {
        session_->history().add_user(task.prompt);
        session_->history().add_assistant(result.output);
    }

    state_.store(AgentLifecycle::idle);
    return result;
}

void PersistentAgent::sleep() {
    std::lock_guard lock(mutex_);
    if (state_.load() == AgentLifecycle::sleeping) return;

    log::info_fmt("team agent sleeping: {} ({})", def_.display_name, def_.agent_id);

    sub_rt_.reset();
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
    if (!def_.tools.empty()) {
        sub_config_.tool_filter_default = def_.tools;
    }
    if (def_.max_steps > 0) {
        sub_config_.default_max_steps = def_.max_steps;
    }
}

} // namespace ben_gear::team
