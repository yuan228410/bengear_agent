#pragma once

#include "ben_gear/agent/plan_manager.hpp"
#include "ben_gear/tool/types.hpp"
#include "ben_gear/llm/usage.hpp"

#include <string_view>

namespace ben_gear::agent {

/// Agent 回调接口 — UI 层实现，Agent 层调用
///
/// 设计原则：
///   1. 纯虚接口，UI 层（CLI/Web/API）各自实现
///   2. 回调只传递结构化数据，不含格式化/ANSI 码
///   3. Agent 层通过回调通知 UI 事件，UI 自行决定展示方式
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

    // ---- 计划模式回调 ----

    /// 检测到 LLM 输出了计划（普通模式自动规划或计划模式输出）
    /// UI 可展示步骤列表并提示用户确认
    virtual void on_plan_detected(const container::Vector<PlanStep>& /*steps*/) const {}

    /// 进入计划模式
    virtual void on_plan_mode_entered() const {}

    /// 退出计划模式
    virtual void on_plan_mode_exited() const {}

    /// 步骤开始执行
    virtual void on_step_started(const PlanStep& /*step*/, int /*total*/) const {}

    /// 步骤执行完成
    virtual void on_step_completed(const PlanStep& /*step*/) const {}

    /// 步骤跳过
    virtual void on_step_skipped(const PlanStep& /*step*/) const {}

    /// 计划全部完成
    virtual void on_plan_completed() const {}

    // ---- 响应统计回调 ----

    /// LLM 响应完成后的 token 用量和延迟统计
    /// UI 展示用，不包含格式化，只传结构化数据
    virtual void on_response_stats(const llm::TokenUsage& /*usage*/,
                                   const llm::RequestLatency& /*latency*/) const {}
};

/// 空回调实现（默认无操作）
class NullAgentCallbacks : public AgentCallbacks {};

} // namespace ben_gear::agent

namespace ben_gear {
using AgentCallbacks = agent::AgentCallbacks;
using NullAgentCallbacks = agent::NullAgentCallbacks;
}
