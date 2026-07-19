#include "server/core/event_bridge.hpp"
#include "base/log/logger.hpp"
#include "base/net/event_loop.hpp"
#include "base/utils/json.hpp"
#include "capabilities/tool/types.hpp"
#include "llm/usage.hpp"
#include "orchestration/serializer.hpp"

#include <string>
#include <utility>

namespace ben_gear::server {

EventBridge::EventBridge(std::shared_ptr<WsHandler> ws,
                         std::string session_id,
                         std::string workspace,
                         std::string user,
                         bool include_thinking,
                         bool include_tool_calls,
                         orchestration::TodoManager* todo_manager,
                         workspace::HistoryDB* history_db)
    : ws_(std::move(ws)),
      session_id_(std::move(session_id)),
      workspace_(std::move(workspace)),
      user_(std::move(user)),
      include_thinking_(include_thinking),
      include_tool_calls_(include_tool_calls),
      todo_manager_(todo_manager),
      history_db_(history_db) {}

EventBridge::~EventBridge() = default;

void EventBridge::subscribe_to(base::EventBus& event_bus) {
    if (subscribed_) return;
    subscribed_ = true;
    token_sub_ = event_bus.subscribe<agent::TokenEvent>(
        [this](const auto& e) { on_token(e); });
    thinking_sub_ = event_bus.subscribe<agent::ThinkingEvent>(
        [this](const auto& e) { on_thinking(e); });
    tool_call_sub_ = event_bus.subscribe<agent::ToolCallEvent>(
        [this](const auto& e) { on_tool_call(e); });
    tool_result_sub_ = event_bus.subscribe<agent::ToolResultEvent>(
        [this](const auto& e) { on_tool_result(e); });
    tool_blocked_sub_ = event_bus.subscribe<agent::ToolBlockedEvent>(
        [this](const auto& e) { on_tool_blocked(e); });
    stats_sub_ = event_bus.subscribe<agent::ResponseStatsEvent>(
        [this](const auto& e) { on_stats(e); });
    exec_event_sub_ = event_bus.subscribe<agent::ExecutionPlanEvent>(
        [this](const auto& e) { on_exec_event(e); });
    todo_sub_ = event_bus.subscribe<agent::TodoUpdateEvent>(
        [this](const auto& e) { on_todo_update(e); });
    sub_start_sub_ = event_bus.subscribe<agent::SubAgentStartEvent>(
        [this](const auto& e) { on_sub_start(e); });
    sub_progress_sub_ = event_bus.subscribe<agent::SubAgentProgressEvent>(
        [this](const auto& e) { on_sub_progress(e); });
    sub_complete_sub_ = event_bus.subscribe<agent::SubAgentCompleteEvent>(
        [this](const auto& e) { on_sub_complete(e); });
    sub_error_sub_ = event_bus.subscribe<agent::SubAgentErrorEvent>(
        [this](const auto& e) { on_sub_error(e); });
}

// ─── 事件处理 ─────────────────────────────────────────────────────

void EventBridge::on_event(const domain::DomainEvent& event) const {
    // EventBus 已覆盖所有 Agent 事件，domain sink 只转发非 Agent 事件
    if (event.source_is(domain::event_source::workflow) || event.source_is(domain::event_source::workflow_task)) {
        return;  // WorkflowEngine 事件不在此处理
    }
    // Agent 事件已通过 EventBus 订阅处理，此处忽略避免重复
}

void EventBridge::on_token(const agent::TokenEvent& e) const {
    if (e.token.empty()) return;
    send(WsMessage::token(session_id_, std::string(e.token)));
}

void EventBridge::on_thinking(const agent::ThinkingEvent& e) const {
    if (!include_thinking_) return;
    send(WsMessage::thinking(session_id_, static_cast<int>(e.token.size()), 0.0,
                             std::string(e.token)));
}

void EventBridge::on_stats(const agent::ResponseStatsEvent& e) const {
    std::lock_guard lock(stats_mutex_);
    response_usage_json_ = build_usage_json(e.usage, e.model_name, e.context_length);
    response_latency_ = e.latency;
    has_response_stats_ = true;
}

void EventBridge::on_tool_call(const agent::ToolCallEvent& e) const {
    if (!include_tool_calls_) return;
    send(WsMessage::tool_call(session_id_, e.call.name, e.call.arguments.dump()));
}

void EventBridge::on_tool_result(const agent::ToolResultEvent& e) const {
    if (!include_tool_calls_) return;
    send(WsMessage::tool_result(session_id_, e.result.name,
                                std::string(e.result.output.data(), e.result.output.size()), 0.0));
}

void EventBridge::on_tool_blocked(const agent::ToolBlockedEvent& e) const {
    orchestration::ExecutionEvent event;
    event.execution_id = std::string("tool-blocked:") + std::string(e.tool_name);
    event.kind = orchestration::ExecutionKind::tool;
    event.type = orchestration::ExecutionEventType::failed;
    event.status = orchestration::ExecutionStatus::failed;
    event.message = std::string(e.reason);
    event.payload.set_field(orchestration::execution_field::tool_name, e.tool_name);
    event.payload.set_field("reason", e.reason);
    event.payload.set_field("category", "approval_block");

    auto payload = orchestration::to_json_string(event);
    send(WsMessage::execution_event(session_id_,
                                    std::string(payload.data(), payload.size())));
}

void EventBridge::on_exec_event(const agent::ExecutionPlanEvent& e) const {
    auto payload = orchestration::to_json_string(e.event);
    send(WsMessage::execution_event(session_id_,
                                    std::string(payload.data(), payload.size())));
}

void EventBridge::on_todo_update(const agent::TodoUpdateEvent& e) const {
    if (!todo_manager_) return;
    if (e.action == "clear") {
        clear_todo_state();
        return;
    }
    std::unique_lock<std::mutex> lock;
    if (state_mutex_) lock = std::unique_lock<std::mutex>(*state_mutex_);
    auto next = e.item;
    if (next.session_id.empty()) next.session_id = session_id_;
    if (next.workspace.empty()) next.workspace = workspace_;
    if (next.todo_id.empty()) next.todo_id = next.title;
    auto a = e.action.empty() ? std::string("updated") : std::string(e.action);
    auto delta = todo_manager_->upsert(std::move(next), std::move(a));
    persist_todo_state();
    if (lock.owns_lock()) lock.unlock();
    emit_todo_delta(delta);
}

void EventBridge::on_sub_start(const agent::SubAgentStartEvent&) const {}
void EventBridge::on_sub_progress(const agent::SubAgentProgressEvent&) const {}
void EventBridge::on_sub_complete(const agent::SubAgentCompleteEvent&) const {}
void EventBridge::on_sub_error(const agent::SubAgentErrorEvent&) const {}

// ─── Stats ────────────────────────────────────────────────────────

bool EventBridge::has_response_stats() const {
    std::lock_guard lock(stats_mutex_);
    return has_response_stats_;
}

std::string EventBridge::response_usage_json() const {
    std::lock_guard lock(stats_mutex_);
    return response_usage_json_.empty() ? std::string("{}") : response_usage_json_;
}

llm::RequestLatency EventBridge::response_latency() const {
    std::lock_guard lock(stats_mutex_);
    return response_latency_;
}

// ─── Todo lifecycle ───────────────────────────────────────────────

void EventBridge::emit_todo_state() const {
    if (!todo_manager_) return;
    auto payload = orchestration::to_json_string(todo_manager_->state());
    auto msg = WsMessage::todo_state(session_id_,
                                     std::string(payload.data(), payload.size()));
    msg.strings[std::string("workspace")] = workspace_;
    send(std::move(msg));
}

void EventBridge::clear_todo_state() const {
    if (!todo_manager_) return;
    todo_manager_->reset(session_id_, workspace_);
    persist_todo_state();
    emit_todo_state();
}

void EventBridge::persist_todo_state() const {
    if (!todo_manager_ || !history_db_) return;
    auto payload = orchestration::to_json_string(todo_manager_->state());
    history_db_->save_session_state_async(session_id_, std::string("todo"),
                                          std::string(payload.data(), payload.size()));
}

void EventBridge::emit_todo_delta(const orchestration::TodoDelta& delta) const {
    auto payload = orchestration::to_json_string(delta);
    auto msg = WsMessage::todo_delta(session_id_,
                                     std::string(payload.data(), payload.size()));
    msg.strings[std::string("workspace")] = workspace_;
    send(std::move(msg));
}

// ─── Internal ─────────────────────────────────────────────────────

void EventBridge::send(WsMessage msg) const {
    if (!ws_ || !ws_->alive()) {
        log::warn_fmt("EventBridge: ws not alive, dropping msg type={}", msg.type);
        return;
    }
    if (!workspace_.empty()) {
        msg.strings[std::string("workspace")] = workspace_;
    }
    auto json = msg.to_json();
    auto& loop = ws_->loop();
    if (loop.is_loop_thread()) {
        ws_->queue_send(std::move(json));
    } else {
        auto handler = ws_;
        loop.submit_task([handler, json = std::move(json)]() mutable {
            if (handler && handler->alive()) {
                handler->queue_send(std::move(json));
            }
        });
    }
}

std::string EventBridge::build_usage_json(const llm::TokenUsage& usage,
                                           std::string_view model_name,
                                           int64_t context_length) {
    Json j;
    j["prompt_tokens"] = usage.prompt_tokens;
    j["completion_tokens"] = usage.completion_tokens;
    j["total_tokens"] = usage.total_tokens;
    if (!model_name.empty()) j["model"] = std::string(model_name);
    if (context_length > 0) j["context_length"] = context_length;
    auto dumped = j.dump();
    return std::string(dumped.data(), dumped.size());
}

} // namespace ben_gear::server
