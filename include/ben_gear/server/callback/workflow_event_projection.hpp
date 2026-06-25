#pragma once

#include "ben_gear/domain/event.hpp"
#include "ben_gear/orchestration/event.hpp"
#include "ben_gear/orchestration/todo.hpp"

#include <string>

namespace ben_gear::server {

struct WorkflowEventProjection {
    orchestration::ExecutionEvent execution_event;
    std::string workflow_id;
    std::string task_id;
};

struct WorkflowTodoProjection {
    enum class Kind {
        none,
        start,
        progress,
        finish,
    };

    Kind kind = Kind::none;
    base::container::String todo_id;
    std::string task_id;
    domain::ParentEventId parent_id;
    orchestration::TodoStatus status = orchestration::TodoStatus::pending;
    base::container::String action;
    int progress = 0;
};

WorkflowEventProjection project_workflow_event(const domain::DomainEvent& domain_event);
WorkflowTodoProjection project_workflow_todo(const domain::DomainEvent& domain_event,
                                             const WorkflowEventProjection& workflow_projection);
base::container::String todo_id_for_task(std::string_view workflow_id, std::string_view task_id);

} // namespace ben_gear::server
