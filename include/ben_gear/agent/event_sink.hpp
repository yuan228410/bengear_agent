#pragma once

#include "ben_gear/agent/plan_manager.hpp"
#include "ben_gear/base/container/string.hpp"
#include "ben_gear/domain/event.hpp"
#include "ben_gear/orchestration/event.hpp"
#include "ben_gear/orchestration/todo.hpp"

#include <string_view>

namespace ben_gear::agent {

namespace container = base::container;

/// Agent 领域事件出口。
///
/// 设计原则：
/// 1. 核心层只发布结构化事件，不感知 CLI/Web/API/ANSI/HTTP 等展示细节。
/// 2. 外层 adapter 可把事件投递到 UI、日志、WebSocket 或测试探针。
/// 3. 事件类型表达领域事实：LLM 输出、模式变更、工具执行、统计、子任务、TODO。
/// 4. 不在该接口中引入任何 UI 类型，保持 core/domain 可复用。
class AgentEventSink : public domain::EventSink {
public:
    virtual ~AgentEventSink() = default;

    void on_event(const domain::DomainEvent& /*event*/) const override {}

    // ---- LLM 输出事件 ----

    /// LLM token（流式/非流式共用）
    virtual void on_token(std::string_view token) const { on_event(domain::DomainEvent::token(token)); }

    /// LLM thinking token
    virtual void on_thinking(std::string_view token) const { on_event(domain::DomainEvent::thinking(token)); }

    /// 工具调用开始
    virtual void on_tool_call(const llm::ToolCallRequest& call) const { on_event(domain::DomainEvent::tool_call(call)); }

    /// 工具调用完成
    virtual void on_tool_result(const llm::ToolCallResult& result) const { on_event(domain::DomainEvent::tool_result(result)); }

    // ---- 模式变更（结构化事件） ----

    /// 计划模式变更：normal ↔ planning
    virtual void on_mode_changed(PlanManager::Mode mode) const {
        on_event(domain::DomainEvent::mode_changed(
            mode == PlanManager::Mode::planning ? container::String("planning") : container::String("normal")));
    }

    // ---- 工具拦截（结构化事件） ----

    /// plan 模式下非 read_only 工具被拦截
    virtual void on_tool_blocked(std::string_view tool_name,
                                  std::string_view reason) const {
        on_event(domain::DomainEvent::tool_blocked(
            container::String(tool_name.data(), tool_name.size()),
            container::String(reason.data(), reason.size())));
    }

    // ---- 响应统计事件 ----

    /// LLM 响应完成后的 token 用量和延迟统计
    virtual void on_response_stats(const llm::TokenUsage& usage,
                                    const llm::RequestLatency& latency,
                                    std::string_view model_name = {},
                                    int64_t context_length = 0) const {
        on_event(domain::DomainEvent::usage(
            usage, latency,
            container::String(model_name.data(), model_name.size()),
            context_length));
    }

    // ---- 统一执行结构化事件 ----

    /// 通用执行事件（UI 无关，sub-agent/workflow/tool 统一出口）
    virtual void on_execution_event(const orchestration::ExecutionEvent& /*event*/) const {}

    /// LLM 主动更新结构化 TODO（UI 无关，会话状态由上层实现）
    virtual void on_todo_update(const orchestration::TodoItem& /*item*/,
                                std::string_view /*action*/) const {}

    /// 当前会话 TODO 摘要：Agent 可追加到本轮模型输入尾部，不污染持久化历史。
    virtual base::container::String todo_context_summary() const { return {}; }
};

/// 空事件汇聚实现（默认无操作）
class NullAgentEventSink : public AgentEventSink {};

} // namespace ben_gear::agent

namespace ben_gear {
using AgentEventSink = agent::AgentEventSink;
using NullAgentEventSink = agent::NullAgentEventSink;
}
