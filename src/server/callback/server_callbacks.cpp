#include "ben_gear/server/callback/server_callbacks.hpp"
#include "ben_gear/base/net/event_loop.hpp"
#include "ben_gear/base/log/logger.hpp"
#include "ben_gear/orchestration/serializer.hpp"

#include <string>
#include <utility>

namespace ben_gear::server {

namespace {

container::String to_cs(std::string_view value) {
    return container::String(value.data(), value.size());
}

void put_field(orchestration::ExecutionEvent& event, std::string_view key, std::string_view value) {
    event.payload.fields[to_cs(key)] = to_cs(value);
}

void put_field(orchestration::ExecutionEvent& event, std::string_view key, int value) {
    event.payload.fields[to_cs(key)] = container::String(std::to_string(value));
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

std::string task_execution_id(const std::string& execution_id, const std::string& task_id) {
    return execution_id + ":task:" + task_id;
}

container::String todo_id_for_task(std::string_view workflow_id, std::string_view task_id) {
    container::String id("workflow:");
    id.append(workflow_id);
    id.append(":task:");
    id.append(task_id);
    return id;
}

orchestration::ExecutionStatus workflow_status_to_execution(workflow::WorkflowStatus status) {
    switch (status) {
    case workflow::WorkflowStatus::SUCCESS: return orchestration::ExecutionStatus::succeeded;
    case workflow::WorkflowStatus::FAILED: return orchestration::ExecutionStatus::failed;
    case workflow::WorkflowStatus::CANCELLED: return orchestration::ExecutionStatus::cancelled;
    case workflow::WorkflowStatus::PAUSED: return orchestration::ExecutionStatus::paused;
    case workflow::WorkflowStatus::PENDING: return orchestration::ExecutionStatus::pending;
    case workflow::WorkflowStatus::RUNNING: return orchestration::ExecutionStatus::running;
    }
    return orchestration::ExecutionStatus::failed;
}

orchestration::ExecutionEventType terminal_type(orchestration::ExecutionStatus status) {
    switch (status) {
    case orchestration::ExecutionStatus::succeeded: return orchestration::ExecutionEventType::completed;
    case orchestration::ExecutionStatus::cancelled: return orchestration::ExecutionEventType::cancelled;
    case orchestration::ExecutionStatus::timeout: return orchestration::ExecutionEventType::timeout;
    case orchestration::ExecutionStatus::skipped: return orchestration::ExecutionEventType::skipped;
    case orchestration::ExecutionStatus::paused: return orchestration::ExecutionEventType::paused;
    case orchestration::ExecutionStatus::failed:
    case orchestration::ExecutionStatus::pending:
    case orchestration::ExecutionStatus::running: return orchestration::ExecutionEventType::failed;
    }
    return orchestration::ExecutionEventType::failed;
}

const char* plan_mode_name(agent::PlanManager::Mode mode) {
    switch (mode) {
    case agent::PlanManager::Mode::normal: return "normal";
    case agent::PlanManager::Mode::planning: return "planning";
    }
    return "unknown";
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

ServerCallbacks::ServerCallbacks(std::shared_ptr<WsHandler> ws,
                                 const container::String& session_id,
                                 const container::String& workspace,
                                 orchestration::TodoManager* todo_manager,
                                 ::ben_gear::workspace::HistoryDB* history_db)
    : ws_(std::move(ws)),
      session_id_(session_id),
      workspace_(workspace),
      todo_manager_(todo_manager),
      history_db_(history_db) {}

void ServerCallbacks::on_token(std::string_view token) const {
    send(WsMessage::token(session_id_, container::String(token)));
}
void ServerCallbacks::on_thinking(std::string_view token) const {
    send(WsMessage::thinking(session_id_, static_cast<int>(token.size()), 0.0, container::String(token)));
}
void ServerCallbacks::on_tool_call(const llm::ToolCallRequest& call) const {
    send(WsMessage::tool_call(session_id_, call.name, call.arguments.dump()));
}
void ServerCallbacks::on_tool_result(const llm::ToolCallResult& result) const {
    send(WsMessage::tool_result(session_id_, result.name, std::string(result.output.data(), result.output.size()), 0.0));
}
std::string ServerCallbacks::build_usage_json(const llm::TokenUsage& usage,
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

void ServerCallbacks::on_response_stats(const llm::TokenUsage& usage, const llm::RequestLatency& latency,
                                         std::string_view model_name, int64_t context_length) const {
    std::lock_guard lock(stats_mutex_);
    response_usage_json_ = build_usage_json(usage, model_name, context_length);
    response_latency_ = latency;
    has_response_stats_ = true;
}
void ServerCallbacks::on_execution_event(const orchestration::ExecutionEvent& event) const {
    auto payload = orchestration::to_json_string(event);
    send(WsMessage::execution_event(session_id_, std::string(payload.data(), payload.size())));
}
void ServerCallbacks::on_sub_agent_event(const agent::SubAgentEvent&) const {
}
void ServerCallbacks::on_mode_changed(agent::PlanManager::Mode mode) const {
    const auto mode_name = plan_mode_name(mode);
    auto event = make_event(std::string("plan:") + std::string(mode_name),
                            orchestration::ExecutionKind::approval,
                            mode == agent::PlanManager::Mode::planning
                                ? orchestration::ExecutionEventType::started
                                : orchestration::ExecutionEventType::completed,
                            mode == agent::PlanManager::Mode::planning
                                ? orchestration::ExecutionStatus::running
                                : orchestration::ExecutionStatus::succeeded,
                            mode == agent::PlanManager::Mode::planning
                                ? "Plan mode enabled"
                                : "Plan mode completed");
    put_field(event, "mode", mode_name);
    put_field(event, "category", "planning");
    on_execution_event(event);
}
void ServerCallbacks::on_tool_blocked(std::string_view tool_name, std::string_view reason) const {
    auto event = make_event(std::string("tool-blocked:") + std::string(tool_name),
                            orchestration::ExecutionKind::tool,
                            orchestration::ExecutionEventType::failed,
                            orchestration::ExecutionStatus::failed,
                            reason);
    put_field(event, "tool_name", tool_name);
    put_field(event, "reason", reason);
    put_field(event, "category", "approval_block");
    on_execution_event(event);
}
void ServerCallbacks::on_todo_update(const orchestration::TodoItem& item, std::string_view action) const {
    if (!todo_manager_) return;
    if (action == "clear") {
        clear_todo_state();
        return;
    }
    auto next = item;
    if (next.session_id.empty()) next.session_id = session_id_;
    if (next.workspace.empty()) next.workspace = workspace_;
    if (next.todo_id.empty()) next.todo_id = next.title;
    auto delta = todo_manager_->upsert(std::move(next), to_cs(action.empty() ? "updated" : action));
    emit_todo_delta(delta);
    persist_todo_state();
}

container::String ServerCallbacks::todo_context_summary() const {
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
void ServerCallbacks::on_workflow_started(const std::string& workflow_id,
                                          const std::string& execution_id,
                                          int total) {
    auto event = make_event(execution_id,
                            orchestration::ExecutionKind::workflow,
                            orchestration::ExecutionEventType::started,
                            orchestration::ExecutionStatus::running,
                            "Workflow started");
    put_field(event, "workflow_id", workflow_id);
    put_field(event, "total", total);
    on_execution_event(event);
}
void ServerCallbacks::on_workflow_progress(const std::string& workflow_id,
                                           const std::string& execution_id,
                                           int completed,
                                           int total) {
    auto event = make_event(execution_id,
                            orchestration::ExecutionKind::workflow,
                            orchestration::ExecutionEventType::progress,
                            orchestration::ExecutionStatus::running,
                            "Workflow progress");
    put_field(event, "workflow_id", workflow_id);
    put_field(event, "completed", completed);
    put_field(event, "total", total);
    on_execution_event(event);
}
void ServerCallbacks::on_workflow_completed(const std::string& workflow_id,
                                            const std::string& execution_id,
                                            const workflow::WorkflowState& state) {
    const auto status = workflow_status_to_execution(state.status);
    auto event = make_event(execution_id,
                            orchestration::ExecutionKind::workflow,
                            terminal_type(status),
                            status,
                            state.error_message.empty() ? "Workflow completed" : state.error_message);
    put_field(event, "workflow_id", workflow_id);
    put_field(event, "workflow_status", workflow::workflow_status_name(state.status));
    put_field(event, "completed", static_cast<int>(state.task_results.size()));
    on_execution_event(event);
}
void ServerCallbacks::on_task_started(const std::string& workflow_id,
                                      const std::string& execution_id,
                                      const std::string& task_id,
                                      int total) {
    auto event = make_event(task_execution_id(execution_id, task_id),
                            orchestration::ExecutionKind::task,
                            orchestration::ExecutionEventType::started,
                            orchestration::ExecutionStatus::running,
                            "Task started");
    event.parent_id = to_cs(execution_id);
    event.trace_id = to_cs(workflow_id);
    put_field(event, "workflow_id", workflow_id);
    put_field(event, "task_id", task_id);
    put_field(event, "task_name", task_id);
    put_field(event, "total", total);
    on_execution_event(event);
    if (todo_manager_ && !todo_manager_->empty()) {
        orchestration::TodoItem item;
        item.todo_id = todo_id_for_task(workflow_id, task_id);
        item.session_id = session_id_;
        item.workspace = workspace_;
        item.title = to_cs(task_id);
        item.active_form = to_cs(task_id);
        item.parent_id = to_cs(execution_id);
        item.status = orchestration::TodoStatus::running;
        item.progress = 0;
        auto delta = todo_manager_->upsert(std::move(item), container::String("started"));
        emit_todo_delta(delta);
        persist_todo_state();
    }
}
void ServerCallbacks::on_task_progress(const std::string& workflow_id,
                                       const std::string& execution_id,
                                       const std::string& task_id,
                                       int progress) {
    auto event = make_event(task_execution_id(execution_id, task_id),
                            orchestration::ExecutionKind::task,
                            orchestration::ExecutionEventType::progress,
                            orchestration::ExecutionStatus::running,
                            "Task progress");
    event.parent_id = to_cs(execution_id);
    event.trace_id = to_cs(workflow_id);
    put_field(event, "workflow_id", workflow_id);
    put_field(event, "task_id", task_id);
    put_field(event, "task_name", task_id);
    put_field(event, "progress", progress);
    on_execution_event(event);
    if (todo_manager_ && !todo_manager_->empty()) {
        auto delta = todo_manager_->update_status(
            todo_id_for_task(workflow_id, task_id),
            orchestration::TodoStatus::running,
            container::String("progress"),
            progress);
        emit_todo_delta(delta);
        persist_todo_state();
    }
}
void ServerCallbacks::on_task_completed(const std::string& workflow_id,
                                        const std::string& execution_id,
                                        const std::string& task_id,
                                        const workflow::TaskResult& result) {
    auto event = make_event(task_execution_id(execution_id, task_id),
                            orchestration::ExecutionKind::task,
                            result.success ? orchestration::ExecutionEventType::completed : orchestration::ExecutionEventType::failed,
                            result.success ? orchestration::ExecutionStatus::succeeded : orchestration::ExecutionStatus::failed,
                            result.success ? "Task completed" : result.error_message);
    event.parent_id = to_cs(execution_id);
    event.trace_id = to_cs(workflow_id);
    put_field(event, "workflow_id", workflow_id);
    put_field(event, "task_id", task_id);
    put_field(event, "task_name", task_id);
    put_field(event, "success", result.success ? "true" : "false");
    on_execution_event(event);
    if (todo_manager_ && !todo_manager_->empty()) {
        auto delta = todo_manager_->update_status(
            todo_id_for_task(workflow_id, task_id),
            result.success ? orchestration::TodoStatus::succeeded : orchestration::TodoStatus::failed,
            result.success ? container::String("completed") : container::String(result.error_message.c_str()),
            result.success ? 100 : 0);
        emit_todo_delta(delta);
        persist_todo_state();
    }
}
void ServerCallbacks::set_session_id(const container::String& sid) { session_id_ = sid; }
bool ServerCallbacks::ws_alive() const { return ws_ && ws_->alive(); }
bool ServerCallbacks::has_response_stats() const {
    std::lock_guard lock(stats_mutex_);
    return has_response_stats_;
}
std::string ServerCallbacks::response_usage_json() const {
    std::lock_guard lock(stats_mutex_);
    return response_usage_json_.empty() ? std::string("{}") : response_usage_json_;
}
llm::RequestLatency ServerCallbacks::response_latency() const {
    std::lock_guard lock(stats_mutex_);
    return response_latency_;
}
WsMessage ServerCallbacks::enrich(WsMessage msg) const {
    if (!workspace_.empty()) msg.strings[container::String("workspace")] = workspace_;
    return msg;
}
void ServerCallbacks::persist_todo_state() const {
    if (!todo_manager_ || !history_db_) return;
    auto payload = orchestration::to_json_string(todo_manager_->state());
    history_db_->save_session_state(workspace_, session_id_, container::String("todo"), payload);
}
void ServerCallbacks::emit_todo_state() const {
    if (!todo_manager_) return;
    auto payload = orchestration::to_json_string(todo_manager_->state());
    send(WsMessage::todo_state(session_id_, std::string(payload.data(), payload.size())));
}
void ServerCallbacks::clear_todo_state() const {
    if (!todo_manager_) return;
    todo_manager_->reset(session_id_, workspace_);
    persist_todo_state();
    emit_todo_state();
}
void ServerCallbacks::emit_todo_delta(const orchestration::TodoDelta& delta) const {
    auto payload = orchestration::to_json_string(delta);
    send(WsMessage::todo_delta(session_id_, std::string(payload.data(), payload.size())));
}
void ServerCallbacks::send(const WsMessage& msg) const {
    auto enriched = enrich(msg);
    if (!ws_ || !ws_->alive()) {
        log::warn_fmt("ServerCallbacks: ws not alive, dropping msg type={} session={}",
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
