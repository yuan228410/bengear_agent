#include "ben_gear/server/callback/workflow_event_projection.hpp"

#include <charconv>
#include <string>

namespace ben_gear::server {

namespace {

base::container::String to_cs(std::string_view value) {
    return base::container::String(value.data(), value.size());
}

std::string field_string(const domain::DomainEvent& event, std::string_view key) {
    const auto value = event.field_view(key);
    return std::string(value.data(), value.size());
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

orchestration::ExecutionKind execution_kind_for(const domain::DomainEvent& event) {
    return event.source_is(domain::event_source::workflow_task)
        ? orchestration::ExecutionKind::task
        : orchestration::ExecutionKind::workflow;
}

orchestration::ExecutionEventType execution_type_for(const domain::DomainEvent& event) {
    if (event.type_is(domain::event_type::started)) return orchestration::ExecutionEventType::started;
    if (event.type_is(domain::event_type::completed)) return orchestration::ExecutionEventType::completed;
    if (event.type_is(domain::event_type::failed)) return orchestration::ExecutionEventType::failed;
    return orchestration::ExecutionEventType::progress;
}

orchestration::ExecutionStatus execution_status_for(const domain::DomainEvent& event) {
    if (event.status_is(domain::event_status::succeeded)) return orchestration::ExecutionStatus::succeeded;
    if (event.status_is(domain::event_status::failed)) return orchestration::ExecutionStatus::failed;
    if (event.status_is(domain::event_status::cancelled)) return orchestration::ExecutionStatus::cancelled;
    if (event.status_is(domain::event_status::paused)) return orchestration::ExecutionStatus::paused;
    return orchestration::ExecutionStatus::running;
}

int parse_progress(std::string_view value) {
    int progress = 0;
    const auto* first = value.data();
    const auto* last = first + value.size();
    const auto result = std::from_chars(first, last, progress);
    if (result.ec != std::errc{} || result.ptr != last) {
        return 0;
    }
    return progress;
}

} // namespace

base::container::String todo_id_for_task(std::string_view workflow_id, std::string_view task_id) {
    base::container::String id("workflow:");
    id.append(workflow_id);
    id.append(":task:");
    id.append(task_id);
    return id;
}

WorkflowEventProjection project_workflow_event(const domain::DomainEvent& domain_event) {
    WorkflowEventProjection projection;
    const auto execution_id = std::string(domain_event.entity_id.data(), domain_event.entity_id.size());
    projection.workflow_id = field_string(domain_event, domain::event_field::workflow_id);
    projection.task_id = field_string(domain_event, domain::event_field::task_id);
    projection.execution_event = make_event(execution_id,
                                            execution_kind_for(domain_event),
                                            execution_type_for(domain_event),
                                            execution_status_for(domain_event),
                                            domain_event.message_view());
    projection.execution_event.parent_id = domain_event.parent_id;
    projection.execution_event.trace_id = domain_event.trace_id;
    for (const auto& [k, v] : domain_event.fields_view()) {
        projection.execution_event.payload.set_field(k, v);
    }
    return projection;
}

WorkflowTodoProjection project_workflow_todo(const domain::DomainEvent& domain_event,
                                             const WorkflowEventProjection& workflow_projection) {
    WorkflowTodoProjection projection;
    if (workflow_projection.task_id.empty()) return projection;

    projection.todo_id = todo_id_for_task(workflow_projection.workflow_id, workflow_projection.task_id);
    projection.task_id = workflow_projection.task_id;
    projection.parent_id = domain_event.parent_id;

    if (domain_event.type_is(domain::event_type::started)) {
        projection.kind = WorkflowTodoProjection::Kind::start;
        projection.status = orchestration::TodoStatus::running;
        projection.action = base::container::String("started");
        return projection;
    }

    if (domain_event.type_is(domain::event_type::progress)) {
        projection.kind = WorkflowTodoProjection::Kind::progress;
        projection.status = orchestration::TodoStatus::running;
        projection.action = base::container::String("progress");
        projection.progress = parse_progress(domain_event.field_view(domain::event_field::progress));
        return projection;
    }

    if (domain_event.type_is(domain::event_type::completed) || domain_event.type_is(domain::event_type::failed)) {
        const bool ok = domain_event.type_is(domain::event_type::completed);
        projection.kind = WorkflowTodoProjection::Kind::finish;
        projection.status = ok ? orchestration::TodoStatus::succeeded : orchestration::TodoStatus::failed;
        projection.action = ok ? base::container::String("completed") : to_cs(domain_event.message_view());
        projection.progress = ok ? 100 : 0;
        return projection;
    }

    return projection;
}

} // namespace ben_gear::server
