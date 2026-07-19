#pragma once

#include "base/core/event_bus.hpp"
#include "agent/core/events.hpp"
#include "domain/event.hpp"
#include "orchestration/event.hpp"
#include "orchestration/todo.hpp"
#include "server/ws/protocol.hpp"
#include "llm/usage.hpp"
#include "server/ws/handler.hpp"
#include "workspace/history_db.hpp"

#include <memory>
#include <mutex>
#include <string>

namespace ben_gear::server {

/// 事件桥接器 — 订阅 EventBus，将 Agent 事件转换为 WebSocket 消息
class EventBridge : public domain::EventSink {
public:
    EventBridge(std::shared_ptr<WsHandler> ws,
                std::string session_id,
                std::string workspace,
                std::string user,
                bool include_thinking,
                bool include_tool_calls,
                orchestration::TodoManager* todo_manager,
                workspace::HistoryDB* history_db);

    ~EventBridge();

    /// 订阅 EventBus（调用后开始接收事件）
    void subscribe_to(base::EventBus& event_bus);

    // ---- DomainEventSink ----
    void on_event(const domain::DomainEvent& event) const override;

    // ---- Stats ----
    bool has_response_stats() const;
    std::string response_usage_json() const;
    llm::RequestLatency response_latency() const;

    // ---- Todo lifecycle ----
    void set_state_mutex(std::mutex* mutex) { state_mutex_ = mutex; }
    void emit_todo_state() const;
    void clear_todo_state() const;

    bool alive() const { return ws_ && ws_->alive(); }

private:
    void send(WsMessage msg) const;
    void persist_todo_state() const;
    void emit_todo_delta(const orchestration::TodoDelta& delta) const;

    static std::string build_usage_json(const llm::TokenUsage& usage,
                                        std::string_view model_name,
                                        int64_t context_length);

    std::shared_ptr<WsHandler> ws_;
    std::string session_id_;
    std::string workspace_;
    std::string user_;
    bool include_thinking_;
    bool include_tool_calls_;
    orchestration::TodoManager* todo_manager_;
    workspace::HistoryDB* history_db_;
    
    bool subscribed_ = false;  // subscribe_to 是否已调用

    mutable std::mutex stats_mutex_;
    mutable bool has_response_stats_ = false;
    mutable std::string response_usage_json_;
    mutable llm::RequestLatency response_latency_;

    mutable std::mutex* state_mutex_ = nullptr;

    // EventBus 订阅（RAII，析构自动取消）
    base::Subscription token_sub_;
    base::Subscription thinking_sub_;
    base::Subscription tool_call_sub_;
    base::Subscription tool_result_sub_;
    base::Subscription tool_blocked_sub_;
    base::Subscription stats_sub_;
    base::Subscription exec_event_sub_;
    base::Subscription todo_sub_;
    base::Subscription sub_start_sub_;
    base::Subscription sub_progress_sub_;
    base::Subscription sub_complete_sub_;
    base::Subscription sub_error_sub_;

    // 事件处理函数
    void on_token(const agent::TokenEvent& e) const;
    void on_thinking(const agent::ThinkingEvent& e) const;
    void on_tool_call(const agent::ToolCallEvent& e) const;
    void on_tool_result(const agent::ToolResultEvent& e) const;
    void on_tool_blocked(const agent::ToolBlockedEvent& e) const;
    void on_stats(const agent::ResponseStatsEvent& e) const;
    void on_exec_event(const agent::ExecutionPlanEvent& e) const;
    void on_todo_update(const agent::TodoUpdateEvent& e) const;
    void on_sub_start(const agent::SubAgentStartEvent& e) const;
    void on_sub_progress(const agent::SubAgentProgressEvent& e) const;
    void on_sub_complete(const agent::SubAgentCompleteEvent& e) const;
    void on_sub_error(const agent::SubAgentErrorEvent& e) const;
};

} // namespace ben_gear::server
