#pragma once

#include "agent/core/interface/agent_core.hpp"
#include "agent/core/interface/event_sink.hpp"
#include "server/ws/handler.hpp"
#include "server/ws/protocol.hpp"
#include "orchestration/event.hpp"
#include "orchestration/todo.hpp"
#include "workflow/metrics.hpp"
#include "workspace/history_db.hpp"

#include <memory>
#include <mutex>
#include <string>

namespace ben_gear::server {

/// Server 模式回调 — 同时实现 domain::EventSink 和 agent::AgentEventSink
/// 使 WS 路径可复用统一的 Runtime::run_session_async()
class ServerEventSink : public domain::EventSink, public agent::AgentEventSink {
public:
    explicit ServerEventSink(std::shared_ptr<WsHandler> ws,
                             const std::string& session_id,
                             const std::string& workspace,
                             bool include_thinking = false,
                             bool include_tool_calls = false,
                             orchestration::TodoManager* todo_manager = nullptr,
                             ::ben_gear::workspace::HistoryDB* history_db = nullptr);

    void on_event(const domain::DomainEvent& event) const;
    void on_token(std::string_view token) const;
    void on_thinking(std::string_view token) const;
    void on_tool_call(const llm::ToolCallRequest& call) const;
    void on_tool_result(const llm::ToolCallResult& result) const;
    void on_response_stats(const llm::TokenUsage& usage,
                           const llm::RequestLatency& latency,
                           std::string_view model_name = {},
                           int64_t context_length = 0) const;
    void on_execution_event(const orchestration::ExecutionEvent& event) const;
    void on_tool_blocked(std::string_view tool_name, std::string_view reason) const;
    void on_todo_update(const orchestration::TodoItem& item,
                        std::string_view action) const;
    std::string todo_context_summary() const;

    void set_session_id(const std::string& session_id);
    void set_state_mutex(std::mutex* mutex) { state_mutex_ = mutex; }
    bool ws_alive() const;
    bool has_response_stats() const;
    std::string response_usage_json() const;
    llm::RequestLatency response_latency() const;
    WsMessage enrich(WsMessage msg) const;

private:
    void handle_workflow_event(const domain::DomainEvent& event) const;
    void send(const WsMessage& msg) const;
    void persist_todo_state() const;
    void emit_todo_state() const;
    void clear_todo_state() const;
    void emit_todo_delta(const orchestration::TodoDelta& delta) const;
    std::string build_usage_json(const llm::TokenUsage& usage,
                                 std::string_view model_name,
                                 int64_t context_length) const;

    std::shared_ptr<WsHandler> ws_;
    std::string session_id_;
    std::string workspace_;
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

} // namespace ben_gear::server
