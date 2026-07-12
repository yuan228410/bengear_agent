#pragma once

#include "base/container/string.hpp"
#include "base/domain/event.hpp"
#include "base/utils/json.hpp"
#include "llm/usage.hpp"
#include "orchestration/event.hpp"
#include "orchestration/todo.hpp"
#include "tool/types.hpp"

namespace ben_gear::agent {

namespace container = base::container;

/// Agent 事件回调接口（无状态，纯通知）
class AgentEventSink {
public:
    virtual ~AgentEventSink() = default;

    virtual void on_event(const domain::DomainEvent& event) const = 0;
    virtual void on_token(std::string_view token) const = 0;
    virtual void on_thinking(std::string_view token) const = 0;
    virtual void on_tool_call(const llm::ToolCallRequest& call) const = 0;
    virtual void on_tool_result(const llm::ToolCallResult& result) const = 0;
    virtual void on_response_stats(const llm::TokenUsage& usage,
                                   const llm::RequestLatency& latency,
                                   std::string_view model_name = {},
                                   int64_t context_length = 0) const = 0;
    virtual void on_execution_event(const orchestration::ExecutionEvent& event) const = 0;
    virtual void on_tool_blocked(std::string_view tool_name,
                                 std::string_view reason) const = 0;
    virtual void on_todo_update(const orchestration::TodoItem& item,
                                std::string_view action) const = 0;
    virtual container::String todo_context_summary() const = 0;
};

/// 空实现
class NullAgentEventSink : public AgentEventSink {
public:
    NullAgentEventSink();
    ~NullAgentEventSink() override;

    void on_event(const domain::DomainEvent&) const override;
    void on_token(std::string_view) const override;
    void on_thinking(std::string_view) const override;
    void on_tool_call(const llm::ToolCallRequest&) const override;
    void on_tool_result(const llm::ToolCallResult&) const override;
    void on_response_stats(const llm::TokenUsage&, const llm::RequestLatency&,
                           std::string_view, int64_t) const override;
    void on_execution_event(const orchestration::ExecutionEvent&) const override;
    void on_tool_blocked(std::string_view, std::string_view) const override;
    void on_todo_update(const orchestration::TodoItem&, std::string_view) const override;
    container::String todo_context_summary() const override;
};

} // namespace ben_gear::agent

namespace ben_gear {
using AgentEventSink = agent::AgentEventSink;
using NullAgentEventSink = agent::NullAgentEventSink;
}
