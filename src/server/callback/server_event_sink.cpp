#include "server/callback/server_event_sink.hpp"
#include "server/callback/workflow_event_projection.hpp"
#include "base/net/event_loop.hpp"
#include "base/log/logger.hpp"
#include "orchestration/serializer.hpp"

#include <string>
#include <utility>

namespace ben_gear::server {

namespace {

container::String to_cs(std::string_view value) {
    return container::String(value.data(), value.size());
}

void put_field(orchestration::ExecutionEvent& event, std::string_view key, std::string_view value) {
    event.payload.set_field(key, value);
}

orchestration::ExecutionEvent make_event(std::string_view execution_id,
                                          orchestration::ExecutionKind kind,
                                          orchestration::ExecutionEventType type,
                                          orchestration::ExecutionStatus status,
                                          std::string_view message = {}) {
    orchestration::ExecutionEvent event;
    event.execution_id = to_cs(execution_id);
    event.kind = kind;
    event.type = type;
    event.status = status;
    event.message = to_cs(message);
    return event;
}

void append_limited(container::String& out, std::string_view value, size_t max_len) {
    if (value.size() <= max_len) {
        out.append(value);
        return;
    }
    out.append(value.substr(0, max_len));
    out.append("...");
}

} // namespace

ServerEventSink::ServerEventSink(std::shared_ptr<WsHandler> ws,
                                 const container::String& session_id,
                                 const container::String& workspace,
                                 bool include_thinking,
                                 bool include_tool_calls,
                                 orchestration::TodoManager* todo_manager,
                                 ::ben_gear::workspace::HistoryDB* history_db)
    : ws_(std::move(ws)),
      session_id_(session_id),
      workspace_(workspace),
      include_thinking_(include_thinking),
      include_tool_calls_(include_tool_calls),
      todo_manager_(todo_manager),
      history_db_(history_db) {}

void ServerEventSink::on_event(const domain::DomainEvent& event) const {
    if (event.source_is(domain::event_source::workflow) || event.source_is(domain::event_source::workflow_task)) {
        handle_workflow_event(event);
        return;
    }
    if (event.type_is(domain::event_type::token) && std::holds_alternative<container::String>(event.payload)) {
        on_token(std::get<container::String>(event.payload));
    }
}

void ServerEventSink::on_token(std::string_view token) const {
    send(WsMessage::token(session_id_, container::String(token)));
}
void ServerEventSink::on_thinking(std::string_view token) const {
    if (!include_thinking_) return;
    send(WsMessage::thinking(session_id_, static_cast<int>(token.size()), 0.0, container::String(token)));
}
void ServerEventSink::on_tool_call(const llm::ToolCallRequest& call) const {
    if (!include_tool_calls_) return;
    send(WsMessage::tool_call(session_id_, call.name, call.arguments.dump()));
}
void ServerEventSink::on_tool_result(const llm::ToolCallResult& result) const {
    if (!include_tool_calls_) return;
    send(WsMessage::tool_result(session_id_, result.name, std::string(result.output.data(), result.output.size()), 0.0));
}
std::string ServerEventSink::build_usage_json(const llm::TokenUsage& usage,
                                               std::string_view model_name,
                                               int64_t context_length) const {
    std::string j = "{\"prompt_tokens\":" + std::to_string(usage.prompt_tokens)
        + ",\"completion_tokens\":" + std::to_string(usage.completion_tokens)
        + ",\"total_tokens\":" + std::to_string(usage.total_tokens);
    if (!model_name.empty()) j += ",\"model\":\"" + std::string(model_name) + "\"";
    if (context_length > 0) j += ",\"context_length\":" + std::to_string(context_length);
    j += "}";
    return j;
}

void ServerEventSink::on_response_stats(const llm::TokenUsage& usage, const llm::RequestLatency& latency,
                                         std::string_view model_name, int64_t context_length) const {
    std::lock_guard lock(stats_mutex_);
    response_usage_json_ = build_usage_json(usage, model_name, context_length);
    response_latency_ = latency;
    has_response_stats_ = true;
}
void ServerEventSink::on_execution_event(const orchestration::ExecutionEvent& event) const {
    auto payload = orchestration::to_json_string(event);
    send(WsMessage::execution_event(session_id_, std::string(payload.data(), payload.size())));
}
void ServerEventSink::on_tool_blocked(std::string_view tool_name, std::string_view reason) const {
    auto event = make_event(std::string("tool-blocked:") + std::string(tool_name),
                            orchestration::ExecutionKind::tool,
                            orchestration::ExecutionEventType::failed,
                            orchestration::ExecutionStatus::failed,
                            reason);
    put_field(event, orchestration::execution_field::tool_name, tool_name);
    put_field(event, "reason", reason);
    put_field(event, "category", "approval_block");
    on_execution_event(event);
}
void ServerEventSink::on_todo_update(const orchestration::TodoItem& item, std::string_view action) const {
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
    auto delta = todo_manager_->upsert(std::move(next), to_cs(action.empty() ? "updated" : action));
    persist_todo_state();
    lock.unlock();
    emit_todo_delta(delta);
}

container::String ServerEventSink::todo_context_summary() const {
    std::unique_lock<std::mutex> lock;
    if (state_mutex_) lock = std::unique_lock<std::mutex>(*state_mutex_);
    if (!todo_manager_ || todo_manager_->empty()) return {};
    const auto& state = todo_manager_->state();
    container::String out("\n\n[Current TODO state]\n");
    int emitted = 0;
    for (const auto& item : state.items) {
        if (emitted >= 8) break;
        out.append("- ");
        out.append(orchestration::to_string(item.status));
        out.append(": ");
        append_limited(out, std::string_view(item.title.data(), item.title.size()), 96);
        if (!item.result_summary.empty()) {
            out.append(" — ");
            append_limited(out, std::string_view(item.result_summary.data(), item.result_summary.size()), 80);
        }
        out.append("\n");
        ++emitted;
    }
    if (state.items.size() > static_cast<size_t>(emitted)) {
        out.append("- ... ");
        auto remaining = std::to_string(state.items.size() - static_cast<size_t>(emitted));
        out.append(std::string_view(remaining.data(), remaining.size()));
        out.append(" more\n");
    }
    out.append("If the user asks to continue/resume, treat this as the interrupted task state: resume pending/blocked items, avoid repeating succeeded work, and use update_todo to refine TODO granularity only when useful. For unrelated new/simple tasks, do not update TODO unless it clearly helps.");
    return out;
}
void ServerEventSink::handle_workflow_event(const domain::DomainEvent& domain_event) const {
    const auto projection = project_workflow_event(domain_event);
    on_execution_event(projection.execution_event);

    if (!todo_manager_ || todo_manager_->empty() || projection.task_id.empty()) return;

    const auto todo_projection = project_workflow_todo(domain_event, projection);
    if (todo_projection.kind == WorkflowTodoProjection::Kind::none) return;

    if (todo_projection.kind == WorkflowTodoProjection::Kind::start) {
        orchestration::TodoItem item;
        item.todo_id = todo_projection.todo_id;
        item.session_id = session_id_;
        item.workspace = workspace_;
        item.title = to_cs(todo_projection.task_id);
        item.active_form = to_cs(todo_projection.task_id);
        item.parent_id = todo_projection.parent_id;
        item.status = todo_projection.status;
        item.progress = todo_projection.progress;
        auto delta = todo_manager_->upsert(std::move(item), todo_projection.action);
        emit_todo_delta(delta);
        persist_todo_state();
    } else if (todo_projection.kind == WorkflowTodoProjection::Kind::progress) {
        auto delta = todo_manager_->update_status(todo_projection.todo_id,
                                                 todo_projection.status,
                                                 todo_projection.action,
                                                 todo_projection.progress);
        emit_todo_delta(delta);
        persist_todo_state();
    } else if (todo_projection.kind == WorkflowTodoProjection::Kind::finish) {
        auto delta = todo_manager_->update_status(todo_projection.todo_id,
                                                 todo_projection.status,
                                                 todo_projection.action,
                                                 todo_projection.progress);
        emit_todo_delta(delta);
        persist_todo_state();
    }
}
void ServerEventSink::set_session_id(const container::String& sid) { session_id_ = sid; }
bool ServerEventSink::ws_alive() const { return ws_ && ws_->alive(); }
bool ServerEventSink::has_response_stats() const {
    std::lock_guard lock(stats_mutex_);
    return has_response_stats_;
}
std::string ServerEventSink::response_usage_json() const {
    std::lock_guard lock(stats_mutex_);
    return response_usage_json_.empty() ? std::string("{}") : response_usage_json_;
}
llm::RequestLatency ServerEventSink::response_latency() const {
    std::lock_guard lock(stats_mutex_);
    return response_latency_;
}
WsMessage ServerEventSink::enrich(WsMessage msg) const {
    if (!workspace_.empty()) msg.strings[container::String("workspace")] = workspace_;
    return msg;
}
void ServerEventSink::persist_todo_state() const {
    if (!todo_manager_ || !history_db_) return;
    auto payload = orchestration::to_json_string(todo_manager_->state());
    history_db_->save_session_state(workspace_, session_id_, container::String("todo"), payload);
}
void ServerEventSink::emit_todo_state() const {
    if (!todo_manager_) return;
    auto payload = orchestration::to_json_string(todo_manager_->state());
    send(WsMessage::todo_state(session_id_, std::string(payload.data(), payload.size())));
}
void ServerEventSink::clear_todo_state() const {
    if (!todo_manager_) return;
    todo_manager_->reset(session_id_, workspace_);
    persist_todo_state();
    emit_todo_state();
}
void ServerEventSink::emit_todo_delta(const orchestration::TodoDelta& delta) const {
    auto payload = orchestration::to_json_string(delta);
    send(WsMessage::todo_delta(session_id_, std::string(payload.data(), payload.size())));
}
void ServerEventSink::send(const WsMessage& msg) const {
    auto enriched = enrich(msg);
    if (!ws_ || !ws_->alive()) {
        log::warn_fmt("ServerEventSink: ws not alive, dropping msg type={} session={}",
                      enriched.type.c_str(), enriched.session_id.c_str());
        return;
    }
    auto json = enriched.to_json();
    auto ws = ws_;
    auto& loop = ws->loop();

    if (loop.is_loop_thread()) {
        ws->queue_send(std::move(json));
    } else {
        loop.submit_task([ws, json = std::move(json)]() mutable {
            if (ws && ws->alive()) {
                ws->queue_send(std::move(json));
            }
        });
    }
}

} // namespace ben_gear::server
