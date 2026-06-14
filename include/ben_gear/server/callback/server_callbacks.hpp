#pragma once

#include "ben_gear/agent/callbacks.hpp"
#include "ben_gear/server/ws/handler.hpp"
#include "ben_gear/server/ws/protocol.hpp"
#include "ben_gear/base/container/string.hpp"
#include "ben_gear/orchestration/event.hpp"
#include "ben_gear/orchestration/todo.hpp"
#include "ben_gear/workflow/metrics.hpp"
#include "ben_gear/workspace/history_db.hpp"

#include <memory>
#include <mutex>
#include <string>

namespace ben_gear::server {

/// Server 模式回调 — AgentCallbacks → WS 推送
class ServerCallbacks : public agent::AgentCallbacks, public workflow::WorkflowProgressCallbacks {
public:
    explicit ServerCallbacks(std::shared_ptr<WsHandler> ws,
                             const container::String& session_id,
                             const container::String& workspace,
                             orchestration::TodoManager* todo_manager = nullptr,
                             ::ben_gear::workspace::HistoryDB* history_db = nullptr);

    void on_token(std::string_view token) const override;
    void on_thinking(std::string_view token) const override;
    void on_tool_call(const llm::ToolCallRequest& call) const override;
    void on_tool_result(const llm::ToolCallResult& result) const override;
    void on_response_stats(const llm::TokenUsage& usage,
                           const llm::RequestLatency& latency,
                           std::string_view model_name = {},
                           int64_t context_length = 0) const override;
    void on_execution_event(const orchestration::ExecutionEvent& event) const override;
    void on_sub_agent_event(const agent::SubAgentEvent& event) const override;
    void on_mode_changed(agent::PlanManager::Mode mode) const override;
    void on_tool_blocked(std::string_view tool_name, std::string_view reason) const override;
    void on_todo_update(const orchestration::TodoItem& item,
                        std::string_view action) const override;
    container::String todo_context_summary() const override;

    void on_task_started(const std::string& workflow_id,
                         const std::string& execution_id,
                         const std::string& task_id,
                         int total) override;
    void on_task_progress(const std::string& workflow_id,
                          const std::string& execution_id,
                          const std::string& task_id,
                          int progress) override;
    void on_task_completed(const std::string& workflow_id,
                           const std::string& execution_id,
                           const std::string& task_id,
                           const workflow::TaskResult& result) override;
    void on_workflow_progress(const std::string& workflow_id,
                              const std::string& execution_id,
                              int completed,
                              int total) override;
    void on_workflow_started(const std::string& workflow_id,
                             const std::string& execution_id,
                             int total) override;
    void on_workflow_completed(const std::string& workflow_id,
                               const std::string& execution_id,
                               const workflow::WorkflowState& state) override;

    void set_session_id(const container::String& session_id);
    bool ws_alive() const;
    bool has_response_stats() const;
    std::string response_usage_json() const;
    llm::RequestLatency response_latency() const;
    WsMessage enrich(WsMessage msg) const;

private:
    void send(const WsMessage& msg) const;
    void persist_todo_state() const;
    void emit_todo_state() const;
    void clear_todo_state() const;
    void emit_todo_delta(const orchestration::TodoDelta& delta) const;
    std::string build_usage_json(const llm::TokenUsage& usage,
                                 std::string_view model_name,
                                 int64_t context_length) const;

    std::shared_ptr<WsHandler> ws_;
    container::String session_id_;
    container::String workspace_;
    orchestration::TodoManager* todo_manager_ = nullptr;
    ::ben_gear::workspace::HistoryDB* history_db_ = nullptr;
    mutable std::mutex stats_mutex_;
    mutable bool has_response_stats_ = false;
    mutable std::string response_usage_json_;
    mutable llm::RequestLatency response_latency_;
};

} // namespace ben_gear::server
