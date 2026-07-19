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

// ============================================================

// ============================================================
// DomainEventSink
// ============================================================

void EventBridge::on_event(const domain::DomainEvent& event) const {
    if (event.source_is(domain::event_source::workflow) || event.source_is(domain::event_source::workflow_task)) {
        // 工作流事件由 ws_session_manager 中的 workflow_event_projection 处理
        return;
    }
    try {
        if (event.type_is(domain::event_type::token) && std::holds_alternative<std::string>(event.payload)) {
            on_token(std::get<std::string>(event.payload));
        } else if (event.type_is(domain::event_type::tool_call) && std::holds_alternative<domain::ToolCallPayload>(event.payload)) {
            const auto& payload = std::get<domain::ToolCallPayload>(event.payload);
            auto j = Json::parse(payload.json);
            acp::ToolCallRequest req;
            req.id = j.value("id", "");
            req.name = j.value("name", "");
            req.arguments = j.contains("arguments") ? j["arguments"] : Json::object();
            on_tool_call(req);
        } else if (event.type_is(domain::event_type::tool_result) && std::holds_alternative<domain::ToolResultPayload>(event.payload)) {
            const auto& payload = std::get<domain::ToolResultPayload>(event.payload);
            auto j = Json::parse(payload.json);
            acp::ToolCallResult result;
            result.tool_call_id = j.value("tool_call_id", "");
            result.name = j.value("name", "");
            result.output = j.value("output", "");
            result.success = j.value("success", true);
            on_tool_result(result);
        } else if (event.type_is(domain::event_type::response_stats) && std::holds_alternative<domain::TokenUsage>(event.payload)) {
            const auto& du = std::get<domain::TokenUsage>(event.payload);
            llm::TokenUsage usage;
            usage.prompt_tokens = du.prompt_tokens;
            usage.completion_tokens = du.completion_tokens;
            usage.total_tokens = du.total_tokens;
            llm::RequestLatency latency;
            auto latency_str = event.field_view(domain::event_field::latency_seconds);
            if (!latency_str.empty()) latency.total_seconds = std::stod(std::string(latency_str));
            auto model = event.field_view(domain::event_field::model);
            auto ctx_str = event.field_view(domain::event_field::context_length);
            int64_t ctx_len = 0;
            if (!ctx_str.empty()) ctx_len = std::stoll(std::string(ctx_str));
            on_response_stats(usage, latency, model, ctx_len);
        }
    } catch(const std::exception& e) {
        log::error_fmt("EventBridge: on_event failed: {}", e.what());
    }
}
// StreamEventSink
// ============================================================

void EventBridge::on_token(std::string_view token) const {
    if (token.empty()) return;
    // 逐 token 即时发送 — 流式实时性优先，帧合并在 WsHandler 传输层自然发生
    send(WsMessage::token(session_id_, std::string(token)));
}


void EventBridge::on_thinking(std::string_view token) const {
    if (!include_thinking_) return;
    send(WsMessage::thinking(session_id_, static_cast<int>(token.size()), 0.0,
                             std::string(token)));
}

void EventBridge::on_response_stats(const llm::TokenUsage& usage,
                                     const llm::RequestLatency& latency,
                                     std::string_view model_name,
                                     int64_t context_length) const {

    std::lock_guard lock(stats_mutex_);
    response_usage_json_ = build_usage_json(usage, model_name, context_length);
    response_latency_ = latency;
    has_response_stats_ = true;
}

// ============================================================
// ToolEventSink
// ============================================================

void EventBridge::on_tool_call(const acp::ToolCallRequest& call) const {
    if (!include_tool_calls_) return;
    send(WsMessage::tool_call(session_id_, call.name, call.arguments.dump()));
}

void EventBridge::on_tool_result(const acp::ToolCallResult& result) const {
    if (!include_tool_calls_) return;
    send(WsMessage::tool_result(session_id_, result.name,
                                std::string(result.output.data(), result.output.size()), 0.0));
}

void EventBridge::on_tool_blocked(std::string_view tool_name, std::string_view reason) const {
    auto event = make_blocked_event(tool_name, reason);
    on_execution_event(event);
}

// ============================================================
// OrchestrationEventSink
// ============================================================

void EventBridge::on_execution_event(const orchestration::ExecutionEvent& event) const {
    auto payload = orchestration::to_json_string(event);
    send(WsMessage::execution_event(session_id_,
                                    std::string(payload.data(), payload.size())));
}

void EventBridge::on_sub_agent_start(const std::string&, const std::string&) const {}
void EventBridge::on_sub_agent_progress(const std::string&, const std::string&) const {}
void EventBridge::on_sub_agent_complete(const std::string&, const std::string&) const {}
void EventBridge::on_sub_agent_error(const std::string&, const std::string&) const {}

void EventBridge::on_todo_update(const orchestration::TodoItem& item,
                                  std::string_view action) const {
    if (!todo_manager_) return;
    if (action == "clear") {
        clear_todo_state();
        return;
    }
    std::unique_lock<std::mutex> lock;
    if (state_mutex_) lock = std::unique_lock<std::mutex>(*state_mutex_);
    auto next = item;
    if (next.session_id.empty()) next.session_id = session_id_;
    if (next.workspace.empty()) next.workspace = workspace_;
    if (next.todo_id.empty()) next.todo_id = next.title;
    auto a = action.empty() ? std::string("updated") : std::string(action);
    auto delta = todo_manager_->upsert(std::move(next), std::move(a));
    persist_todo_state();
    if (lock.owns_lock()) lock.unlock();
    emit_todo_delta(delta);
}

// ============================================================
// Stats
// ============================================================

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

// ============================================================
// Todo lifecycle
// ============================================================

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

// ============================================================
// Internal
// ============================================================

void EventBridge::send(WsMessage msg) const {
    if (!ws_ || !ws_->alive()) {
        log::warn_fmt("EventBridge: ws not alive, dropping msg type={}", msg.type);
        return;
    }
    // 注入 workspace 元数据
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
    // 使用项目 Json 库而非手动字符串拼接，保证合法 JSON
    Json j;
    j["prompt_tokens"] = usage.prompt_tokens;
    j["completion_tokens"] = usage.completion_tokens;
    j["total_tokens"] = usage.total_tokens;
    if (!model_name.empty()) j["model"] = std::string(model_name);
    if (context_length > 0) j["context_length"] = context_length;
    auto dumped = j.dump();
    return std::string(dumped.data(), dumped.size());
}

orchestration::ExecutionEvent EventBridge::make_blocked_event(std::string_view tool_name,
                                                               std::string_view reason) {
    orchestration::ExecutionEvent event;
    event.execution_id = std::string("tool-blocked:") + std::string(tool_name);
    event.kind = orchestration::ExecutionKind::tool;
    event.type = orchestration::ExecutionEventType::failed;
    event.status = orchestration::ExecutionStatus::failed;
    event.message = std::string(reason);
    event.payload.set_field(orchestration::execution_field::tool_name, tool_name);
    event.payload.set_field("reason", reason);
    event.payload.set_field("category", "approval_block");
    return event;
}

} // namespace ben_gear::server
