#include "ben_gear/server/callback/workflow_event_projection.hpp"
#include "ben_gear/test/test_framework.hpp"

using namespace ben_gear;

namespace {

domain::DomainEvent workflow_task_event(std::string_view type,
                                         std::string_view status = domain::event_status::running,
                                         std::string_view message = {}) {
    auto event = domain::DomainEvent::make(domain::event_source::workflow_task,
                                           type,
                                           domain::EventPayload{},
                                           message);
    event.set_status(status);
    event.entity_id = domain::EntityId("task-exec-1");
    event.parent_id = domain::ParentEventId("workflow-exec-1");
    event.trace_id = domain::TraceId("trace-1");
    event.set_field(domain::event_field::workflow_id, "wf-1");
    event.set_field(domain::event_field::task_id, "task-a");
    return event;
}

} // namespace

TEST(ServerWorkflowEventProjectionTest, ProjectsTaskExecutionEventWithPayloadFields) {
    auto event = workflow_task_event(domain::event_type::progress);
    event.set_field(domain::event_field::progress, "42");

    auto projection = server::project_workflow_event(event);

    EXPECT_EQ(projection.workflow_id, "wf-1");
    EXPECT_EQ(projection.task_id, "task-a");
    EXPECT_EQ(projection.execution_event.execution_id, "task-exec-1");
    EXPECT_EQ(projection.execution_event.parent_id, "workflow-exec-1");
    EXPECT_EQ(projection.execution_event.trace_id, "trace-1");
    EXPECT_EQ(projection.execution_event.kind, orchestration::ExecutionKind::task);
    EXPECT_EQ(projection.execution_event.type, orchestration::ExecutionEventType::progress);
    EXPECT_EQ(projection.execution_event.status, orchestration::ExecutionStatus::running);
    EXPECT_EQ(projection.execution_event.payload.field_view(domain::event_field::workflow_id), "wf-1");
    EXPECT_EQ(projection.execution_event.payload.field_view(domain::event_field::task_id), "task-a");
    EXPECT_EQ(projection.execution_event.payload.field_view(domain::event_field::progress), "42");
}

TEST(ServerWorkflowEventProjectionTest, ProjectsWorkflowExecutionKindAndTerminalStatus) {
    auto event = domain::DomainEvent::make(domain::event_source::workflow,
                                           domain::event_type::completed,
                                           domain::EventPayload{},
                                           "done");
    event.set_status(domain::event_status::succeeded);
    event.entity_id = domain::EntityId("workflow-exec-1");
    event.set_field(domain::event_field::workflow_id, "wf-1");

    auto projection = server::project_workflow_event(event);

    EXPECT_EQ(projection.workflow_id, "wf-1");
    EXPECT_TRUE(projection.task_id.empty());
    EXPECT_EQ(projection.execution_event.kind, orchestration::ExecutionKind::workflow);
    EXPECT_EQ(projection.execution_event.type, orchestration::ExecutionEventType::completed);
    EXPECT_EQ(projection.execution_event.status, orchestration::ExecutionStatus::succeeded);
    EXPECT_EQ(projection.execution_event.message, "done");
}

TEST(ServerWorkflowEventProjectionTest, ProjectsTodoLifecycleActions) {
    auto started = workflow_task_event(domain::event_type::started);
    auto started_event = server::project_workflow_event(started);
    auto started_todo = server::project_workflow_todo(started, started_event);

    EXPECT_EQ(started_todo.kind, server::WorkflowTodoProjection::Kind::start);
    EXPECT_EQ(started_todo.todo_id, "workflow:wf-1:task:task-a");
    EXPECT_EQ(started_todo.task_id, "task-a");
    EXPECT_EQ(started_todo.parent_id, "workflow-exec-1");
    EXPECT_EQ(started_todo.status, orchestration::TodoStatus::running);
    EXPECT_EQ(started_todo.action, "started");
    EXPECT_EQ(started_todo.progress, 0);

    auto progress = workflow_task_event(domain::event_type::progress);
    progress.set_field(domain::event_field::progress, "67");
    auto progress_todo = server::project_workflow_todo(progress, server::project_workflow_event(progress));

    EXPECT_EQ(progress_todo.kind, server::WorkflowTodoProjection::Kind::progress);
    EXPECT_EQ(progress_todo.status, orchestration::TodoStatus::running);
    EXPECT_EQ(progress_todo.action, "progress");
    EXPECT_EQ(progress_todo.progress, 67);

    auto completed = workflow_task_event(domain::event_type::completed, domain::event_status::succeeded);
    auto completed_todo = server::project_workflow_todo(completed, server::project_workflow_event(completed));

    EXPECT_EQ(completed_todo.kind, server::WorkflowTodoProjection::Kind::finish);
    EXPECT_EQ(completed_todo.status, orchestration::TodoStatus::succeeded);
    EXPECT_EQ(completed_todo.action, "completed");
    EXPECT_EQ(completed_todo.progress, 100);
}

TEST(ServerWorkflowEventProjectionTest, ProjectsFailedTodoActionWithMessage) {
    auto failed = workflow_task_event(domain::event_type::failed, domain::event_status::failed, "boom");
    auto todo = server::project_workflow_todo(failed, server::project_workflow_event(failed));

    EXPECT_EQ(todo.kind, server::WorkflowTodoProjection::Kind::finish);
    EXPECT_EQ(todo.status, orchestration::TodoStatus::failed);
    EXPECT_EQ(todo.action, "boom");
    EXPECT_EQ(todo.progress, 0);
}

TEST(ServerWorkflowEventProjectionTest, InvalidProgressFallsBackToZero) {
    auto event = workflow_task_event(domain::event_type::progress);
    event.set_field(domain::event_field::progress, "12x");

    auto todo = server::project_workflow_todo(event, server::project_workflow_event(event));

    EXPECT_EQ(todo.kind, server::WorkflowTodoProjection::Kind::progress);
    EXPECT_EQ(todo.progress, 0);
}

TEST(ServerWorkflowEventProjectionTest, SkipsTodoProjectionWhenTaskIdMissing) {
    auto event = domain::DomainEvent::make(domain::event_source::workflow,
                                           domain::event_type::progress,
                                           domain::EventPayload{},
                                           "workflow only");
    event.set_status(domain::event_status::running);
    event.set_field(domain::event_field::workflow_id, "wf-1");

    auto todo = server::project_workflow_todo(event, server::project_workflow_event(event));

    EXPECT_EQ(todo.kind, server::WorkflowTodoProjection::Kind::none);
}
