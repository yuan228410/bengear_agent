#pragma once

#include "ben_gear/agent/shared_resources.hpp"
#include "ben_gear/agent/callbacks.hpp"
#include "ben_gear/config/settings.hpp"
#include "ben_gear/llm/message.hpp"
#include "ben_gear/tool/manager.hpp"
#include "ben_gear/tool/registry.hpp"
#include "ben_gear/workspace/session.hpp"
#include "ben_gear/base/log/logger.hpp"
#include "ben_gear/base/net/event_loop.hpp"

#include <atomic>
#include <memory>
#include <string>
#include <utility>
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
        resources_->post_init();  // 注册需要 shared_from_this 的工具（工作流）
        setup_tool_timeouts();
    }

    /// 设置长耗时工具的超时覆盖
    void setup_tool_timeouts() {
        // 工作流工具涉及多轮 LLM 调用，默认 30s 超时不够
        tool_manager_.set_tool_timeout(
            base::container::String("execute_workflow"),
            std::chrono::seconds(resources_->settings().agent.workflow_timeout));
        tool_manager_.set_tool_timeout(
            base::container::String("get_workflow_status"),
            std::chrono::seconds(resources_->settings().agent.workflow_status_timeout));
    }

    /// 设置是否启用会话记忆
    void set_enable_memory(bool enable) {
        enable_memory_.store(enable, std::memory_order_relaxed);
    }

    bool enable_memory() const noexcept {
        return enable_memory_.load(std::memory_order_relaxed);
    }

    /// 基于 Session 的异步聊天（线程安全，Session 独占 history）
    /// 实现细节在 agent.cpp 中
    net::Task<llm::ChatResult> run_session_async(net::EventLoop& loop,
                                                  workspace::Session& session,
                                                  base::container::String prompt,
                                                  const AgentCallbacks& callbacks,
                                                  const net::CancellationToken& cancel = {});

    /// 获取共享资源
    std::shared_ptr<SharedResources> resources() const noexcept { return resources_; }

    /// 获取配置
    const config::Settings& settings() const noexcept { return resources_->settings(); }

    /// 获取工具注册表
    const llm::ToolRegistry& tools() const noexcept { return resources_->tools(); }

    /// 获取技能加载器
    const skill::SkillLoader& skill_loader() const noexcept { return resources_->skill_loader(); }

    /// 获取记忆存储
    const memory::MemoryStore& memory_store() const noexcept { return *resources_->memory_store(); }

    /// 获取历史数据库
    workspace::HistoryDB& history_db() noexcept { return resources_->history_db(); }

    /// 获取工作空间上下文
    const workspace::WorkspaceContext& workspace_context() const noexcept { return resources_->workspace_context(); }

    /// 获取工作空间管理器
    workspace::WorkspaceManager& workspace_manager() noexcept { return *resources_->workspace_manager(); }

    /// 注册自定义工具
    void register_tool(const base::container::String& name,
                      const base::container::String& description,
                      const base::container::Vector<std::pair<base::container::String, llm::ToolParameterSchema>>& parameters,
                      llm::ToolExecutor executor) {
        resources_->register_tool(name, description, parameters, std::move(executor));
    }

private:
    /// 流式步骤循环
    net::Task<llm::ChatResult> run_session_stream_step(
            net::EventLoop& loop, workspace::Session& session,
            llm::ConversationHistory& history,
            const container::String& prompt_text,
            const AgentCallbacks& callbacks,
            const net::CancellationToken& cancel);

    /// 持久化工具步骤
    void persist_tool_step(workspace::Session& session,
                           llm::ConversationHistory& history,
                           const std::vector<llm::ToolCallRequest>& calls,
                           const std::vector<llm::ToolCallResult>& results);

    // 共享资源（一次构建，多 Agent/多会话复用）
    std::shared_ptr<SharedResources> resources_;

    // Per-Agent 状态
    llm::ToolCallManager tool_manager_;
    std::atomic<bool> enable_memory_;
};

}  // namespace ben_gear::agent

namespace ben_gear {
using Agent = agent::Agent;
using AgentCallbacks = agent::AgentCallbacks;
using NullAgentCallbacks = agent::NullAgentCallbacks;
using SharedResources = agent::SharedResources;
}  // namespace ben_gear
