#pragma once

#include "ben_gear/agent/shared_resources.hpp"
#include "ben_gear/agent/callbacks.hpp"
#include "ben_gear/tool/manager.hpp"
#include "ben_gear/workspace/session.hpp"

#include <atomic>
#include <memory>
#include <chrono>

namespace ben_gear::agent {

namespace container = base::container;

/// Agent 类 — 无状态调度器
/// 不持有 ConversationHistory，run_async 接受 Session 引用
/// 共享只读资源通过 SharedResources 管理，多 Agent/多会话可复用
class Agent {
public:
    /// 从 SharedResources 构造（支持多 Agent 共享资源）
    Agent(std::shared_ptr<SharedResources> resources)
        : resources_(std::move(resources)),
          tool_manager_(resources_->tools(), resources_->core_pool(),
                        std::chrono::seconds(resources_->settings().agent.command_timeout),
                        resources_),
          enable_memory_(true) {
        setup_tool_timeouts();
    }

    /// 从 Settings + WorkspaceContext 构造（内部创建 SharedResources）
    Agent(config::Settings settings, workspace::WorkspaceContext ws_ctx)
        : resources_(std::make_shared<SharedResources>(std::move(settings), std::move(ws_ctx))),
          tool_manager_(resources_->tools(), resources_->core_pool(),
                        std::chrono::seconds(resources_->settings().agent.command_timeout),
                        resources_),
          enable_memory_(true) {
        resources_->post_init();
        setup_tool_timeouts();
    }

    void setup_tool_timeouts() {
        tool_manager_.set_tool_timeout(
            base::container::String("execute_workflow"),
            std::chrono::seconds(resources_->settings().agent.workflow_timeout));
        tool_manager_.set_tool_timeout(
            base::container::String("get_workflow_status"),
            std::chrono::seconds(resources_->settings().agent.workflow_status_timeout));
    }

    void set_enable_memory(bool enable) {
        enable_memory_.store(enable, std::memory_order_relaxed);
    }

    bool enable_memory() const noexcept {
        return enable_memory_.load(std::memory_order_relaxed);
    }

    struct RunOptions {
        base::container::String system_prompt;
        base::container::String model_override;
        int max_tool_steps = 0;
        std::chrono::milliseconds timeout{0};
    };

    /// 基于 Session 的异步聊天
    net::Task<llm::ChatResult> run_session_async(net::EventLoop& loop,
                                                  workspace::Session& session,
                                                  base::container::String prompt,
                                                  const AgentCallbacks& callbacks,
                                                  const net::CancellationToken& cancel = {},
                                                  const llm::ToolRegistry* tool_override = nullptr);

    /// 基于 Session 的异步聊天（可覆盖单次执行选项，供 sub-agent/workflow 使用）
    net::Task<llm::ChatResult> run_session_async(net::EventLoop& loop,
                                                  workspace::Session& session,
                                                  base::container::String prompt,
                                                  const AgentCallbacks& callbacks,
                                                  RunOptions options,
                                                  const net::CancellationToken& cancel = {},
                                                  const llm::ToolRegistry* tool_override = nullptr);

    // ---- 资源访问 ----

    std::shared_ptr<SharedResources> resources() const noexcept { return resources_; }
    const config::Settings& settings() const noexcept { return resources_->settings(); }
    const llm::ToolRegistry& tools() const noexcept { return resources_->tools(); }
    const skill::SkillLoader& skill_loader() const noexcept { return resources_->skill_loader(); }
    const memory::MemoryStore& memory_store() const noexcept { return *resources_->memory_store(); }
    workspace::HistoryDB& history_db() noexcept { return resources_->history_db(); }
    const workspace::WorkspaceContext& workspace_context() const noexcept { return resources_->workspace_context(); }
    workspace::WorkspaceManager& workspace_manager() noexcept { return *resources_->workspace_manager(); }

    // ---- 计划模式 ----

    PlanManager& plan_manager() noexcept { return plan_manager_; }
    const PlanManager& plan_manager() const noexcept { return plan_manager_; }

    // ---- 工具注册 ----

    void register_tool(const base::container::String& name,
                       const base::container::String& description,
                       const base::container::Vector<std::pair<base::container::String, llm::ToolParameterSchema>>& parameters,
                       llm::ToolExecutor executor) {
        resources_->register_tool(name, description, parameters, std::move(executor));
    }

private:
    net::Task<llm::ChatResult> run_session_stream_step(
        net::EventLoop& loop, workspace::Session& session,
        workspace::ConversationHistory& history,
        const AgentCallbacks& callbacks,
        const net::CancellationToken& cancel,
        const llm::ToolRegistry* tool_override,
        const RunOptions& options);

    void persist_tool_step(workspace::Session& session,
                           workspace::ConversationHistory& history,
                           const std::vector<llm::ToolCallRequest>& calls,
                           const std::vector<llm::ToolCallResult>& results,
                           const container::String& assistant_text);

    std::shared_ptr<SharedResources> resources_;
    llm::ToolCallManager tool_manager_;
    PlanManager plan_manager_;
    std::atomic<bool> enable_memory_{true};
};

} // namespace ben_gear::agent

namespace ben_gear {
using Agent = agent::Agent;
using AgentCallbacks = agent::AgentCallbacks;
using NullAgentCallbacks = agent::NullAgentCallbacks;
using SharedResources = agent::SharedResources;
}
