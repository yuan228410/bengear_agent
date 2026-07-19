#pragma once

#include "agent/core/event_sink.hpp"
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

/// 事件桥接器 — 将 Agent 领域事件直接转换为 WebSocket 消息
///
/// 合并了原 EventCollector + WsEventSerializer + ServerEventSink 的功能：
/// - 实现四层 EventSink 接口（Domain + Stream + Tool + Orchestration）
/// - 直接序列化到 WS 帧，无中间对象
/// - 内置 token 批处理
/// - build_usage_json 使用 Json 库
class EventBridge final : public domain::EventSink,
                           public agent::StreamEventSink,
                           public agent::ToolEventSink,
                           public agent::OrchestrationEventSink,
                           public agent::SubAgentEventSink {
public:
    EventBridge(std::shared_ptr<WsHandler> ws,
                std::string session_id,
                std::string workspace,
                std::string user,
                bool include_thinking,
                bool include_tool_calls,
                orchestration::TodoManager* todo_manager,
                workspace::HistoryDB* history_db);

    // ---- DomainEventSink ----
    void on_event(const domain::DomainEvent& event) const override;

    // ---- StreamEventSink ----
    void on_token(std::string_view token) const override;
    void on_thinking(std::string_view token) const override;
    void on_response_stats(const llm::TokenUsage& usage,
                           const llm::RequestLatency& latency,
                           std::string_view model_name = {},
                           int64_t context_length = 0) const override;

    // ---- ToolEventSink ----
    void on_tool_call(const acp::ToolCallRequest& call) const override;
    void on_tool_result(const acp::ToolCallResult& result) const override;
    void on_tool_blocked(std::string_view tool_name,
                         std::string_view reason) const override;

    // ---- OrchestrationEventSink ----
    void on_execution_event(const orchestration::ExecutionEvent& event) const override;
    void on_todo_update(const orchestration::TodoItem& item,
                        std::string_view action) const override;

    // ---- SubAgentEventSink ----
    void on_sub_agent_start(const std::string& task_id,
                            const std::string& prompt) const override;
    void on_sub_agent_progress(const std::string& task_id,
                               const std::string& info) const override;
    void on_sub_agent_complete(const std::string& task_id,
                               const std::string& summary) const override;
    void on_sub_agent_error(const std::string& task_id,
                            const std::string& error) const override;

    // ---- Stats ----
    bool has_response_stats() const;
    std::string response_usage_json() const;
    llm::RequestLatency response_latency() const;

    // ---- Todo lifecycle ----
    void set_state_mutex(std::mutex* mutex) { state_mutex_ = mutex; }
    void emit_todo_state() const;
    void clear_todo_state() const;

    // ---- Accessors ----
    bool alive() const { return ws_ && ws_->alive(); }

private:
    void send(WsMessage msg) const;
    void persist_todo_state() const;
    void emit_todo_delta(const orchestration::TodoDelta& delta) const;

    static std::string build_usage_json(const llm::TokenUsage& usage,
                                        std::string_view model_name,
                                        int64_t context_length);
    static orchestration::ExecutionEvent make_blocked_event(std::string_view tool_name,
                                                             std::string_view reason);

    std::shared_ptr<WsHandler> ws_;
    std::string session_id_;
    std::string workspace_;
    std::string user_;
    bool include_thinking_;
    bool include_tool_calls_;
    orchestration::TodoManager* todo_manager_;
    workspace::HistoryDB* history_db_;

    mutable std::mutex stats_mutex_;
    mutable bool has_response_stats_ = false;
    mutable std::string response_usage_json_;
    mutable llm::RequestLatency response_latency_;

    mutable std::mutex* state_mutex_ = nullptr;
};

inline agent::AgentEventSinks as_agent_sinks(EventBridge& bridge) {
    return {bridge, bridge, bridge, bridge};
}

} // namespace ben_gear::server
