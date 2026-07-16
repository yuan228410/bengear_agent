#include "agent/runtime/application/runtime_execution.hpp"

#include <utility>

namespace ben_gear::application {

namespace {

core::RuntimeStatus to_runtime_status(ExecutionStatus status) {
    switch (status) {
    case ExecutionStatus::planned: return core::RuntimeStatus::planned;
    case ExecutionStatus::running: return core::RuntimeStatus::running;
    case ExecutionStatus::succeeded: return core::RuntimeStatus::succeeded;
    case ExecutionStatus::failed: return core::RuntimeStatus::failed;
    case ExecutionStatus::skipped: return core::RuntimeStatus::skipped;
    }
    return core::RuntimeStatus::planned;
}

core::RuntimeEventKind success_kind_for(ExecutionStepKind) {
    return core::RuntimeEventKind::step_succeeded;
}

std::string step_id(ExecutionStepKind kind) {
    return to_string(kind);
}

ExecutionStep make_step(ExecutionStepKind kind,
                        std::string title,
                        bool required = true,
                        bool mutates_workspace = false,
                        Json metadata = Json::object()) {
    return ExecutionStep{step_id(kind), kind, title, required, mutates_workspace, std::move(metadata)};
}

ExecutionTraceEvent trace_event(const ExecutionStep& step,
                                ExecutionStatus status,
                                const domain::AppError* error = nullptr,
                                Json details = Json::object()) {
    ExecutionTraceEvent event;
    event.step_id = step.step_id;
    event.kind = step.kind;
    event.status = status;
    if (error) {
        event.error_type = error->code;
        event.message = error->message;
        if (!error->details_json.empty()) {
            event.details["error_details"] = error->details_json;
        }
    }
    event.details = std::move(details);
    return event;
}

core::RuntimeEvent runtime_event(const ExecutionRequest& request,
                               const ExecutionPlan& plan,
                               const ExecutionStep& step,
                               core::RuntimeEventKind kind,
                               ExecutionStatus status,
                               std::string message = {},
                               Json details = Json::object()) {
    core::RuntimeEvent event;
    event.request_id = request.request_id;
    event.operation_id = plan.boundary.operation.operation_id;
    event.step_id = step.step_id;
    event.kind = kind;
    event.status = to_runtime_status(status);
    event.message = std::move(message);
    event.details = std::move(details);
    return event;
}

core::RuntimeBoundary request_boundary(const ExecutionRequest& request) {
    auto boundary = request.boundary;
    if (boundary.operation.operation_id.empty()) {
        boundary.operation = to_runtime_operation(request.command);
    }
    return boundary;
}

} // namespace

std::string to_string(ExecutionStepKind kind) {
    switch (kind) {
    case ExecutionStepKind::validate: return "validate";
    case ExecutionStepKind::authorize: return "authorize";
    case ExecutionStepKind::checkpoint: return "checkpoint";
    case ExecutionStepKind::execute: return "execute";
    case ExecutionStepKind::audit: return "audit";
    }
    return "execute";
}

std::string to_string(ExecutionStatus status) {
    switch (status) {
    case ExecutionStatus::planned: return "planned";
    case ExecutionStatus::running: return "running";
    case ExecutionStatus::succeeded: return "succeeded";
    case ExecutionStatus::failed: return "failed";
    case ExecutionStatus::skipped: return "skipped";
    }
    return "planned";
}

Json to_json(const ExecutionStep& step) {
    return Json{{"step_id", step.step_id},
                {"kind", to_string(step.kind)},
                {"title", step.title},
                {"required", step.required},
                {"mutates_workspace", step.mutates_workspace},
                {"metadata", step.metadata}};
}

Json to_json(const ExecutionPlan& plan) {
    Json steps = Json::array();
    for (const auto& step : plan.steps) steps.push_back(to_json(step));
    return Json{{"plan_id", plan.plan_id},
                {"boundary", core::to_json(plan.boundary)},
                {"steps", steps},
                {"dry_run", plan.dry_run}};
}

Json to_json(const ExecutionTraceEvent& event) {
    return Json{{"step_id", event.step_id},
                {"kind", to_string(event.kind)},
                {"status", to_string(event.status)},
                {"error_type", event.error_type},
                {"message", event.message},
                {"details", event.details}};
}

Json to_json(const ExecutionResult& result) {
    Json trace = Json::array();
    for (const auto& event : result.trace) trace.push_back(to_json(event));
    return Json{{"request_id", result.request_id},
                {"status", to_string(result.status)},
                {"plan", to_json(result.plan)},
                {"trace", trace},
                {"output", result.output}};
}

ExecutionPlan make_execution_plan(const ExecutionRequest& request) {
    ExecutionPlan plan;
    plan.plan_id = request.request_id;
    if (plan.plan_id.empty()) plan.plan_id = request.command.action;
    plan.boundary = request_boundary(request);
    plan.dry_run = request.dry_run;
    plan.steps.push_back(make_step(ExecutionStepKind::validate, "Validate execution request"));
    plan.steps.push_back(make_step(ExecutionStepKind::authorize,
                                   "Authorize requested runtime operation",
                                   true,
                                   false,
                                   Json{{"scope", core::to_string(plan.boundary.operation.scope)},
                                        {"capability", core::to_string(plan.boundary.operation.capability)}}));
    auto checkpoint_mutates = request.command.mutates_workspace || plan.boundary.operation.scope == core::MutationScope::workspace_write ||
                               plan.boundary.operation.scope == core::MutationScope::repository_write;
    plan.steps.push_back(make_step(ExecutionStepKind::checkpoint, "Create mutation checkpoint", true, checkpoint_mutates));
    plan.steps.push_back(make_step(ExecutionStepKind::execute, "Execute runtime operation", true, request.command.mutates_workspace));
    plan.steps.push_back(make_step(ExecutionStepKind::audit, "Append runtime audit trace", false));
    return plan;
}

RuntimeExecutionKernel::RuntimeExecutionKernel(RuntimeExecutionHooks hooks)
    : hooks_(std::move(hooks)) {}

ExecutionPlan RuntimeExecutionKernel::plan(const ExecutionRequest& request) const {
    return make_execution_plan(request);
}

domain::AppResult<void> RuntimeExecutionKernel::run_void_stage(
    const std::function<domain::AppResult<void>(const ExecutionRequest&, const ExecutionPlan&)>& stage,
    const ExecutionRequest& request,
    const ExecutionPlan& plan) {
    if (!stage) return domain::AppResult<void>::success();
    return stage(request, plan);
}

ExecutionResult RuntimeExecutionKernel::execute(const ExecutionRequest& request) const {
    ExecutionResult result;
    result.request_id = request.request_id;
    result.plan = plan(request);
    result.status = request.dry_run ? ExecutionStatus::planned : ExecutionStatus::running;

    auto emit = [&](const core::RuntimeEvent& event) {
        if (hooks_.event_sink) hooks_.event_sink(event);
    };

    if (request.dry_run) {
        for (const auto& step : result.plan.steps) {
            result.trace.push_back(trace_event(step, ExecutionStatus::planned));
            emit(runtime_event(request, result.plan, step, core::RuntimeEventKind::step_skipped, ExecutionStatus::planned, std::string("dry run")));
        }
        result.output = Json{{"success", true}, {"dry_run", true}};
        return result;
    }

    auto find_step = [&](ExecutionStepKind kind) -> const ExecutionStep& {
        for (const auto& step : result.plan.steps) {
            if (step.kind == kind) return step;
        }
        return result.plan.steps.front();
    };

    auto fail = [&](const ExecutionStep& step, const domain::AppError& error) {
        emit(runtime_event(request, result.plan, step, core::RuntimeEventKind::step_failed, ExecutionStatus::failed, error.message));
        result.trace.push_back(trace_event(step, ExecutionStatus::failed, &error));
        result.status = ExecutionStatus::failed;
        result.output = Json{{"success", false},
                             {"error_type", error.code},
                             {"message", error.message}};
        if (!error.details_json.empty()) result.output["details"] = error.details_json;
        if (hooks_.audit) hooks_.audit(request, result);
    };

    const auto& validate_step = find_step(ExecutionStepKind::validate);
    emit(runtime_event(request, result.plan, validate_step, core::RuntimeEventKind::step_started, ExecutionStatus::running));
    if (auto stage = run_void_stage(hooks_.validate, request, result.plan); !stage.ok()) {
        fail(validate_step, stage.error());
        return result;
    }
    result.trace.push_back(trace_event(validate_step, ExecutionStatus::succeeded));
    emit(runtime_event(request, result.plan, validate_step, success_kind_for(validate_step.kind), ExecutionStatus::succeeded));

    const auto& authorize_step = find_step(ExecutionStepKind::authorize);
    emit(runtime_event(request, result.plan, authorize_step, core::RuntimeEventKind::step_started, ExecutionStatus::running));
    if (auto stage = run_void_stage(hooks_.authorize, request, result.plan); !stage.ok()) {
        fail(authorize_step, stage.error());
        return result;
    }
    result.trace.push_back(trace_event(authorize_step, ExecutionStatus::succeeded));
    emit(runtime_event(request, result.plan, authorize_step, success_kind_for(authorize_step.kind), ExecutionStatus::succeeded));

    for (const auto& step : result.plan.steps) {
        if (step.kind != ExecutionStepKind::checkpoint) continue;
        emit(runtime_event(request, result.plan, step, core::RuntimeEventKind::step_started, ExecutionStatus::running));
        if (auto stage = run_void_stage(hooks_.checkpoint, request, result.plan); !stage.ok()) {
            fail(step, stage.error());
            return result;
        }
        result.trace.push_back(trace_event(step, ExecutionStatus::succeeded));
        emit(runtime_event(request, result.plan, step, success_kind_for(step.kind), ExecutionStatus::succeeded));
    }

    const auto& execute_step = find_step(ExecutionStepKind::execute);
    emit(runtime_event(request, result.plan, execute_step, core::RuntimeEventKind::step_started, ExecutionStatus::running));
    domain::AppResult<Json> execution = hooks_.execute
                                            ? hooks_.execute(request, result.plan)
                                            : domain::AppResult<Json>::success(Json{{"success", true}});
    if (!execution.ok()) {
        fail(execute_step, execution.error());
        return result;
    }
    result.output = execution.value();
    emit(runtime_event(request, result.plan, execute_step, core::RuntimeEventKind::output_produced, ExecutionStatus::running, {}, Json{{"output", result.output}}));
    result.trace.push_back(trace_event(execute_step, ExecutionStatus::succeeded, nullptr, Json{{"output", result.output}}));
    emit(runtime_event(request, result.plan, execute_step, success_kind_for(execute_step.kind), ExecutionStatus::succeeded));
    result.status = ExecutionStatus::succeeded;

    const auto& audit_step = find_step(ExecutionStepKind::audit);
    emit(runtime_event(request, result.plan, audit_step, core::RuntimeEventKind::step_started, ExecutionStatus::running));
    if (hooks_.audit) hooks_.audit(request, result);
    result.trace.push_back(trace_event(audit_step, ExecutionStatus::succeeded));
    emit(runtime_event(request, result.plan, audit_step, success_kind_for(audit_step.kind), ExecutionStatus::succeeded));
    return result;
}

} // namespace ben_gear::application
