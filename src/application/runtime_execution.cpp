#include "ben_gear/application/runtime_execution.hpp"

#include <utility>

namespace ben_gear::application {

namespace {

container::String step_id(ExecutionStepKind kind) {
    return container::String(to_string(kind).c_str());
}

ExecutionStep make_step(ExecutionStepKind kind,
                        std::string title,
                        bool required = true,
                        bool mutates_workspace = false,
                        Json metadata = Json::object()) {
    return ExecutionStep{step_id(kind), kind, container::String(title.c_str()), required, mutates_workspace, std::move(metadata)};
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
        if (!std::string(error->details_json.c_str()).empty()) {
            event.details["error_details"] = std::string(error->details_json.c_str());
        }
    }
    event.details = std::move(details);
    return event;
}

core::RuntimeBoundary request_boundary(const ExecutionRequest& request) {
    auto boundary = request.boundary;
    if (std::string(boundary.operation.operation_id.c_str()).empty()) {
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
    return Json{{"step_id", std::string(step.step_id.c_str())},
                {"kind", to_string(step.kind)},
                {"title", std::string(step.title.c_str())},
                {"required", step.required},
                {"mutates_workspace", step.mutates_workspace},
                {"metadata", step.metadata}};
}

Json to_json(const ExecutionPlan& plan) {
    Json steps = Json::array();
    for (const auto& step : plan.steps) steps.push_back(to_json(step));
    return Json{{"plan_id", std::string(plan.plan_id.c_str())},
                {"boundary", core::to_json(plan.boundary)},
                {"steps", steps},
                {"dry_run", plan.dry_run}};
}

Json to_json(const ExecutionTraceEvent& event) {
    return Json{{"step_id", std::string(event.step_id.c_str())},
                {"kind", to_string(event.kind)},
                {"status", to_string(event.status)},
                {"error_type", std::string(event.error_type.c_str())},
                {"message", std::string(event.message.c_str())},
                {"details", event.details}};
}

Json to_json(const ExecutionResult& result) {
    Json trace = Json::array();
    for (const auto& event : result.trace) trace.push_back(to_json(event));
    return Json{{"request_id", std::string(result.request_id.c_str())},
                {"status", to_string(result.status)},
                {"plan", to_json(result.plan)},
                {"trace", trace},
                {"output", result.output}};
}

ExecutionPlan make_execution_plan(const ExecutionRequest& request) {
    ExecutionPlan plan;
    plan.plan_id = request.request_id;
    if (std::string(plan.plan_id.c_str()).empty()) plan.plan_id = request.command.action;
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

    if (request.dry_run) {
        for (const auto& step : result.plan.steps) {
            result.trace.push_back(trace_event(step, ExecutionStatus::planned));
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
        result.trace.push_back(trace_event(step, ExecutionStatus::failed, &error));
        result.status = ExecutionStatus::failed;
        result.output = Json{{"success", false},
                             {"error_type", std::string(error.code.c_str())},
                             {"message", std::string(error.message.c_str())}};
        if (!std::string(error.details_json.c_str()).empty()) result.output["details"] = std::string(error.details_json.c_str());
        if (hooks_.audit) hooks_.audit(request, result);
    };

    const auto& validate_step = find_step(ExecutionStepKind::validate);
    if (auto stage = run_void_stage(hooks_.validate, request, result.plan); !stage.ok()) {
        fail(validate_step, stage.error());
        return result;
    }
    result.trace.push_back(trace_event(validate_step, ExecutionStatus::succeeded));

    const auto& authorize_step = find_step(ExecutionStepKind::authorize);
    if (auto stage = run_void_stage(hooks_.authorize, request, result.plan); !stage.ok()) {
        fail(authorize_step, stage.error());
        return result;
    }
    result.trace.push_back(trace_event(authorize_step, ExecutionStatus::succeeded));

    for (const auto& step : result.plan.steps) {
        if (step.kind != ExecutionStepKind::checkpoint) continue;
        if (auto stage = run_void_stage(hooks_.checkpoint, request, result.plan); !stage.ok()) {
            fail(step, stage.error());
            return result;
        }
        result.trace.push_back(trace_event(step, ExecutionStatus::succeeded));
    }

    const auto& execute_step = find_step(ExecutionStepKind::execute);
    domain::AppResult<Json> execution = hooks_.execute
                                            ? hooks_.execute(request, result.plan)
                                            : domain::AppResult<Json>::success(Json{{"success", true}});
    if (!execution.ok()) {
        fail(execute_step, execution.error());
        return result;
    }
    result.output = execution.value();
    result.trace.push_back(trace_event(execute_step, ExecutionStatus::succeeded, nullptr, Json{{"output", result.output}}));
    result.status = ExecutionStatus::succeeded;

    const auto& audit_step = find_step(ExecutionStepKind::audit);
    if (hooks_.audit) hooks_.audit(request, result);
    result.trace.push_back(trace_event(audit_step, ExecutionStatus::succeeded));
    return result;
}

} // namespace ben_gear::application
