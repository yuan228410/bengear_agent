#pragma once

#include "agent/core/agent_core.hpp"
#include "agent/core/event_sink.hpp"
#include "orchestration/event.hpp"
#include "orchestration/todo.hpp"
#include "workflow/metrics.hpp"
#include "workspace/history_db.hpp"

#include <memory>
#include <mutex>
#include <string>

namespace ben_gear::server {

class WsEventSerializer;

/// Receives domain/agent events, accumulates stats, and delegates
/// wire-format serialization to WsEventSerializer.
class EventCollector : public domain::EventSink,
                       public agent::StreamEventSink,
                       public agent::ToolEventSink,
                       public agent::OrchestrationEventSink {
public:
    explicit EventCollector(std::shared_ptr<WsEventSerializer> serializer,
                            const std::string& session_id,
                            const std::string& workspace,
                            const std::string& user = "default",
                            bool include_thinking = false,
                            bool include_tool_calls = false,
                            orchestration::TodoManager* todo_manager = nullptr,
                            ::ben_gear::workspace::HistoryDB* history_db = nullptr);

    void on_event(const domain::DomainEvent& event) const override;
    void on_token(std::string_view token) const override;
    void on_thinking(std::string_view token) const override;
    void on_tool_call(const capabilities::tool::ToolCallRequest& call) const override;
    void on_tool_result(const capabilities::tool::ToolCallResult& result) const override;
    void on_response_stats(const llm::TokenUsage& usage,
                           const llm::RequestLatency& latency,
                           std::string_view model_name = {},
                           int64_t context_length = 0) const override;
    void on_execution_event(const orchestration::ExecutionEvent& event) const override;
    void on_tool_blocked(std::string_view tool_name, std::string_view reason) const override;
    void on_todo_update(const orchestration::TodoItem& item,
                        std::string_view action) const override;

    void set_session_id(const std::string& session_id);
    void set_state_mutex(std::mutex* mutex) { state_mutex_ = mutex; }

    bool has_response_stats() const;
    std::string response_usage_json() const;
    llm::RequestLatency response_latency() const;

    /// Access the underlying serializer (e.g. for terminal message enrichment)
    const WsEventSerializer& serializer() const { return *serializer_; }

private:
    void handle_workflow_event(const domain::DomainEvent& event) const;
    void persist_todo_state() const;
    void emit_todo_state() const;
    void clear_todo_state() const;
    void emit_todo_delta(const orchestration::TodoDelta& delta) const;
    std::string build_usage_json(const llm::TokenUsage& usage,
                                 std::string_view model_name,
                                 int64_t context_length) const;

    std::shared_ptr<WsEventSerializer> serializer_;
    std::string session_id_;
    std::string workspace_;
    std::string user_;
    bool include_thinking_ = false;
    bool include_tool_calls_ = false;
    orchestration::TodoManager* todo_manager_ = nullptr;
    ::ben_gear::workspace::HistoryDB* history_db_ = nullptr;
    mutable std::mutex* state_mutex_ = nullptr;
    mutable std::mutex stats_mutex_;
    mutable bool has_response_stats_ = false;
    mutable std::string response_usage_json_;
    mutable llm::RequestLatency response_latency_;
};

/// Build AgentEventSinks view from an EventCollector (three interfaces point to same object)
inline agent::AgentEventSinks as_agent_sinks(EventCollector& sink) {
    return {sink, sink, sink};
}

/// Backward-compatible alias
using ServerEventSink = EventCollector;

} // namespace ben_gear::server
