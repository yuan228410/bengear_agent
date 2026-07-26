#pragma once

#include "team/types.hpp"
#include "team/context.hpp"

#include "agent/sub_agent_types.hpp"
#include "agent/runtime/sub_agent_runtime.hpp"
#include "config/sub_agent_config.hpp"
#include "workspace/session.hpp"
#include "base/core/event_bus.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace ben_gear::team {

/// 长活 Agent — 持久的、有记忆的团队角色
///
/// 每个 Agent 拥有独立的：
/// - Session（历史会话）+ MemoryStore（长期记忆）
/// - SubAgentRuntime（任务执行引擎）
/// - 生命周期管理（空闲 → 工作中 → 休眠）
///
/// 依赖方向：Agent → SubAgentRuntime → (LLM/Tools)
class PersistentAgent {
public:
    /// 构造 Agent
    /// @param def Agent 定义（从 .md 加载）
    /// @param shared_tools 全员共享的工具注册表
    /// @param settings 全局设置
    /// @param provider LLM 客户端
    /// @param event_bus 事件总线（可选）
    PersistentAgent(
        AgentDef def,
        const capabilities::tool::ToolRegistry& shared_tools,
        const config::Settings& settings,
        llm::ProviderClient& provider,
        base::EventBus* event_bus = nullptr);

    ~PersistentAgent();

    /// 唤醒 Agent（从磁盘恢复状态）
    void wakeup();

    /// 执行任务
    /// @return 执行结果
    agent::SubAgentResult execute(const agent::SubAgentTask& task);

    /// 让 Agent 休眠（释放内存，状态持久化到磁盘）
    void sleep();

    /// 当前生命周期状态
    AgentLifecycle state() const { return state_.load(); }

    /// Agent 定义（只读）
    const AgentDef& def() const { return def_; }

    /// 获取状态摘要（给其他 Agent 或前端用）
    struct StatusSummary {
        std::string agent_id;
        std::string name;
        AgentLifecycle state;
        size_t session_count;
        std::string last_error;
    };
    StatusSummary status() const;

private:
    void apply_tool_filter();

    AgentDef def_;
    std::atomic<AgentLifecycle> state_{AgentLifecycle::idle};

    // 执行引擎
    std::unique_ptr<agent::runtime::SubAgentRuntime> sub_rt_;

    // 持久化状态
    std::unique_ptr<workspace::Session> session_;
    config::SubAgentConfig sub_config_;

    // 外部引用
    const capabilities::tool::ToolRegistry* shared_tools_;
    const config::Settings* settings_;
    llm::ProviderClient* provider_;
    base::EventBus* event_bus_;

    mutable std::mutex mutex_;
};

} // namespace ben_gear::team
