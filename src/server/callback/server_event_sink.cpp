#include "server/callback/server_event_sink.hpp"
#include "server/callback/workflow_event_projection.hpp"
#include "server/callback/ws_event_serializer.hpp"
#include "base/log/logger.hpp"
#include "orchestration/serializer.hpp"

#include <string>
#include <utility>

namespace ben_gear::server {

namespace {

std::string to_cs(std::string_view value) {
    return std::string(value);
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

} // namespace

EventCollector::EventCollector(std::shared_ptr<WsEventSerializer> serializer,
                               const std::string& session_id,
                               const std::string& workspace,
                               bool include_thinking,
                               bool include_tool_calls,
                               orchestration::TodoManager* todo_manager,
                               ::ben_gear::workspace::HistoryDB* history_db)
    : serializer_(std::move(serializer)),
      session_id_(session_id),
      workspace_(workspace),
      include_thinking_(include_thinking),
      include_tool_calls_(include_tool_calls),
      todo_manager_(todo_manager),
      history_db_(history_db) {}

void EventCollector::on_event(const domain::DomainEvent& event) const {
    if (event.source_is(domain::event_source::workflow) || event.source_is(domain::event_source::workflow_task)) {
        handle_workflow_event(event);
        return;
    }
    if (event.type_is(domain::event_type::token) && std::holds_alternative<std::string>(event.payload)) {
        on_token(std::get<std::string>(event.payload));
    } else if (event.type_is(domain::event_type::tool_call) && std::holds_alternative<domain::ToolCallPayload>(event.payload)) {
        const auto& payload = std::get<domain::ToolCallPayload>(event.payload);
        auto j = Json::parse(payload.json);
        capabilities::tool::ToolCallRequest req;
        req.id = j.value("id", "");
        req.name = j.value("name", "");
        req.arguments = j.contains("arguments") ? j["arguments"] : Json::object();
        on_tool_call(req);
    } else if (event.type_is(domain::event_type::tool_result) && std::holds_alternative<domain::ToolResultPayload>(event.payload)) {
        const auto& payload = std::get<domain::ToolResultPayload>(event.payload);
        auto j = Json::parse(payload.json);
        capabilities::tool::ToolCallResult result;
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
        if (!latency_str.empty()) {
            latency.total_seconds = std::stod(std::string(latency_str));
        }
        auto model = event.field_view(domain::event_field::model);
        auto ctx_str = event.field_view(domain::event_field::context_length);
        int64_t ctx_len = 0;
        if (!ctx_str.empty()) {
            ctx_len = std::stoll(std::string(ctx_str));
        }
        on_response_stats(usage, latency, model, ctx_len);
    }
}

void EventCollector::on_token(std::string_view token) const {
    serializer_->send_token(session_id_, token);
}

void EventCollector::on_thinking(std::string_view token) const {
    if (!include_thinking_) return;
    serializer_->send_thinking(session_id_, token);
}

void EventCollector::on_tool_call(const capabilities::tool::ToolCallRequest& call) const {
    if (!include_tool_calls_) return;
    serializer_->send_tool_call(session_id_, call);
}

void EventCollector::on_tool_result(const capabilities::tool::ToolCallResult& result) const {
    if (!include_tool_calls_) return;
    serializer_->send_tool_result(session_id_, result);
}

std::string EventCollector::build_usage_json(const llm::TokenUsage& usage,
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

void EventCollector::on_response_stats(const llm::TokenUsage& usage, const llm::RequestLatency& latency,
                                        std::string_view model_name, int64_t context_length) const {
    std::lock_guard lock(stats_mutex_);
    response_usage_json_ = build_usage_json(usage, model_name, context_length);
    response_latency_ = latency;
    has_response_stats_ = true;
}

void EventCollector::on_execution_event(const orchestration::ExecutionEvent& event) const {
    serializer_->send_execution_event(session_id_, event);
}

void EventCollector::on_tool_blocked(std::string_view tool_name, std::string_view reason) const {
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

void EventCollector::on_todo_update(const orchestration::TodoItem& item, std::string_view action) const {
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

void EventCollector::handle_workflow_event(const domain::DomainEvent& domain_event) const {
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

void EventCollector::set_session_id(const std::string& sid) { session_id_ = sid; }

bool EventCollector::has_response_stats() const {
    std::lock_guard lock(stats_mutex_);
    return has_response_stats_;
}

std::string EventCollector::response_usage_json() const {
    std::lock_guard lock(stats_mutex_);
    return response_usage_json_.empty() ? std::string("{}") : response_usage_json_;
}

llm::RequestLatency EventCollector::response_latency() const {
    std::lock_guard lock(stats_mutex_);
    return response_latency_;
}

void EventCollector::persist_todo_state() const {
    if (!todo_manager_ || !history_db_) return;
    auto payload = orchestration::to_json_string(todo_manager_->state());
    history_db_->save_session_state_async(workspace_, session_id_, std::string("todo"), payload);
}

void EventCollector::emit_todo_state() const {
    if (!todo_manager_) return;
    serializer_->send_todo_state(session_id_, todo_manager_->state());
}

void EventCollector::clear_todo_state() const {
    if (!todo_manager_) return;
    todo_manager_->reset(session_id_, workspace_);
    persist_todo_state();
    emit_todo_state();
}

void EventCollector::emit_todo_delta(const orchestration::TodoDelta& delta) const {
    serializer_->send_todo_delta(session_id_, delta);
}

} // namespace ben_gear::server
