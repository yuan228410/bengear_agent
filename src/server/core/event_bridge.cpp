#include "server/core/event_bridge.hpp"
#include <mutex>
#include "log/logger.hpp"
#include "net/event_loop.hpp"
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
    // Agent 事件已通过 EventBus 订阅处理，此处忽略避免重复
    (void)event;
}

void EventBridge::on_token(const agent::TokenEvent& e) const {
    if (e.token.empty()) return;
    send(WsMessage::token(session_id_, e.token));
}

void EventBridge::on_thinking(const agent::ThinkingEvent& e) const {
    if (!include_thinking_) return;
    send(WsMessage::thinking(session_id_, static_cast<int>(e.token.size()), 0.0, e.token));
}

void EventBridge::on_stats(const agent::ResponseStatsEvent& e) const {
    std::lock_guard lock(stats_mutex_);
    response_usage_json_ = build_usage_json_from_fields(
        e.prompt_tokens, e.completion_tokens, e.total_tokens,
        e.model_name, e.context_length);
    response_latency_ = llm::RequestLatency{
        .total_seconds = e.total_seconds,
        .ttfb_seconds = e.ttfb_seconds,
        .has_ttfb = e.has_ttfb};
    has_response_stats_ = true;
}

void EventBridge::on_tool_call(const agent::ToolCallEvent& e) const {
    if (!include_tool_calls_) return;
    send(WsMessage::tool_call(session_id_, e.name, e.args_json));
}

void EventBridge::on_tool_result(const agent::ToolResultEvent& e) const {
    if (!include_tool_calls_) return;
    send(WsMessage::tool_result(session_id_, e.name, e.output, 0.0));
}

void EventBridge::on_tool_blocked(const agent::ToolBlockedEvent& e) const {
    orchestration::ExecutionEvent event;
    event.execution_id = std::string("tool-blocked:") + e.tool_name;
    event.kind = orchestration::ExecutionKind::tool;
    event.type = orchestration::ExecutionEventType::failed;
    event.status = orchestration::ExecutionStatus::failed;
    event.message = e.reason;
    event.payload.set_field(std::string(orchestration::execution_field::tool_name), std::string(e.tool_name));
    event.payload.set_field(std::string("reason"), std::string(e.reason));
    event.payload.set_field("category", "approval_block");

    auto payload = orchestration::to_json_string(event);
    send(WsMessage::execution_event(session_id_,
                                    std::string(payload.data(), payload.size())));
}

void EventBridge::on_exec_event(const agent::ExecutionPlanEvent& e) const {
    send(WsMessage::execution_event(session_id_, e.json_payload));
}

void EventBridge::on_todo_update(const agent::TodoUpdateEvent& e) const {
    if (!todo_manager_) {
        log::warn_fmt("EventBridge: on_todo_update but no todo_manager");
        return;
    }
    log::info_fmt("EventBridge: on_todo_update action={} todo_id={} title={} status={}",
                  e.action, e.todo_id, e.title, e.status);
    if (e.action == "clear") {
        clear_todo_state();
        return;
    }
    std::unique_lock<std::mutex> lock;
    if (state_mutex_) lock = std::unique_lock<std::mutex>(*state_mutex_);
    orchestration::TodoItem next;
    next.todo_id = std::string(e.todo_id);
    next.session_id = e.session_id.empty() ? session_id_ : std::string(e.session_id);
    next.workspace = e.workspace.empty() ? workspace_ : std::string(e.workspace);
    next.title = std::string(e.title);
    next.status = orchestration::todo_status_from_string(std::string_view(e.status.data(), e.status.size()));
    next.progress = e.progress;
    next.result_summary = std::string(e.summary);
    std::string action = e.action.empty() ? std::string("updated") : std::string(e.action);
    auto delta = todo_manager_->upsert(std::move(next), std::move(action));
    // upsert 返回的 delta.session_id 来自 TodoManager::state_.session_id，
    // 非计划模式下可能为空，需从 EventBridge 补全
    if (delta.session_id.empty()) delta.session_id = session_id_;
    if (delta.workspace.empty()) delta.workspace = workspace_;
    if (delta.item.session_id.empty()) delta.item.session_id = session_id_;
    if (delta.item.workspace.empty()) delta.item.workspace = workspace_;
    log::info_fmt("EventBridge: todo_delta item={} items_count={}",
                  delta.item.todo_id, todo_manager_->state().items.size());
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
    log::info_fmt("EventBridge: emit_todo_delta session={}", session_id_);
    log::debug_fmt("EventBridge: todo_delta payload={}", payload);
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
    log::info_fmt("EventBridge: send type={} session={} size={}",
                  msg.type, msg.session_id, json.size());
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

std::string EventBridge::build_usage_json_from_fields(
        int64_t prompt_tokens, int64_t completion_tokens, int64_t total_tokens,
        std::string_view model_name, int64_t context_length) {
    Json j;
    j["prompt_tokens"] = static_cast<int>(prompt_tokens);
    j["completion_tokens"] = static_cast<int>(completion_tokens);
    j["total_tokens"] = static_cast<int>(total_tokens);
    if (!model_name.empty()) j["model"] = std::string(model_name);
    if (context_length > 0) j["context_length"] = context_length;
    auto dumped = j.dump();
    return std::string(dumped.data(), dumped.size());
}

} // namespace ben_gear::server
