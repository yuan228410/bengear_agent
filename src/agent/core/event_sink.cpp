#include "agent/core/interface/event_sink.hpp"

namespace ben_gear::agent {

namespace container = base::container;

NullAgentEventSink::NullAgentEventSink() = default;
NullAgentEventSink::~NullAgentEventSink() = default;
void NullAgentEventSink::on_event(const domain::DomainEvent&) const {}
void NullAgentEventSink::on_token(std::string_view) const {}
void NullAgentEventSink::on_thinking(std::string_view) const {}
void NullAgentEventSink::on_tool_call(const llm::ToolCallRequest&) const {}
void NullAgentEventSink::on_tool_result(const llm::ToolCallResult&) const {}
void NullAgentEventSink::on_response_stats(const llm::TokenUsage&,
                                            const llm::RequestLatency&,
                                            std::string_view, int64_t) const {}
void NullAgentEventSink::on_execution_event(const orchestration::ExecutionEvent&) const {}
void NullAgentEventSink::on_tool_blocked(std::string_view, std::string_view) const {}
void NullAgentEventSink::on_todo_update(const orchestration::TodoItem&,
                                          std::string_view) const {}
std::string NullAgentEventSink::todo_context_summary() const { return {}; }

} // namespace ben_gear::agent
