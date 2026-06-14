#pragma once

#include "ben_gear/agent/plan_manager.hpp"
#include "ben_gear/base/container/string.hpp"
#include "ben_gear/agent/sub_agent.hpp"
#include "ben_gear/orchestration/event.hpp"
#include "ben_gear/orchestration/todo.hpp"

#include <string_view>

namespace ben_gear::agent {

/// Agent 回调接口 — UI 层实现，Agent 层调用
///
/// 设计原则：
/// 1. 纯虚接口，UI 层（CLI/Web/API）各自实现
/// 2. 回调只传递结构化数据，不含格式化/ANSI 码
/// 3. Agent 层通过回调通知 UI 事件，UI 自行决定展示方式
/// 4. 事件类型最小化：LLM 输出 + 模式变更 + 工具拦截 + 统计 + 子 Agent
class AgentCallbacks {
public:
    virtual ~AgentCallbacks() = default;

    // ---- LLM 输出回调 ----

    /// 收到一个 token（流式/非流式共用）
    virtual void on_token(std::string_view /*token*/) const {}

    /// 收到思考内容
    virtual void on_thinking(std::string_view /*token*/) const {}

    /// 工具调用开始
    virtual void on_tool_call(const llm::ToolCallRequest& /*call*/) const {}

    /// 工具调用完成
    virtual void on_tool_result(const llm::ToolCallResult& /*result*/) const {}

    // ---- 模式变更（结构化事件） ----

    /// 计划模式变更：normal ↔ planning
    virtual void on_mode_changed(PlanManager::Mode /*mode*/) const {}

    // ---- 工具拦截（结构化事件） ----

    /// plan 模式下非 read_only 工具被拦截
    virtual void on_tool_blocked(std::string_view /*tool_name*/,
                                  std::string_view /*reason*/) const {}

    // ---- 响应统计回调 ----

    /// LLM 响应完成后的 token 用量和延迟统计
    virtual void on_response_stats(const llm::TokenUsage& /*usage*/,
                                    const llm::RequestLatency& /*latency*/,
                                    std::string_view /*model_name*/ = {},
                                    int64_t /*context_length*/ = 0) const {}

    // ---- 统一执行结构化事件 ----

    /// 通用执行事件（UI 无关，sub-agent/workflow/tool 统一出口）
    virtual void on_execution_event(const orchestration::ExecutionEvent& /*event*/) const {}

    // ---- 子 Agent 结构化事件（迁移期桥接，后续删除专用事件）----

    /// 子 Agent 事件（UI 无关，扩展只需新增 SubAgentEventType 枚举值）
    virtual void on_sub_agent_event(const SubAgentEvent& /*event*/) const {}

    /// LLM 主动更新结构化 TODO（UI 无关，会话状态由上层实现）
    virtual void on_todo_update(const orchestration::TodoItem& /*item*/,
                                std::string_view /*action*/) const {}

    /// 当前会话 TODO 摘要：Agent 可追加到本轮模型输入尾部，不污染持久化历史。
    virtual base::container::String todo_context_summary() const { return {}; }
};

/// 空回调实现（默认无操作）
class NullAgentCallbacks : public AgentCallbacks {};

} // namespace ben_gear::agent

namespace ben_gear {
using AgentCallbacks = agent::AgentCallbacks;
using NullAgentCallbacks = agent::NullAgentCallbacks;
}
