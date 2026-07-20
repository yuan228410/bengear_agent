#include "agent/runtime/exec_types.hpp"

namespace ben_gear::agent::runtime {

std::string to_string(ExecutionStepKind kind) {
    switch (kind) {
    case ExecutionStepKind::validate:   return "validate";
    case ExecutionStepKind::authorize:  return "authorize";
    case ExecutionStepKind::checkpoint: return "checkpoint";
    case ExecutionStepKind::execute:    return "execute";
    case ExecutionStepKind::audit:      return "audit";
    }
    return "execute";
}

std::string to_string(ExecutionStatus status) {
    switch (status) {
    case ExecutionStatus::planned:   return "planned";
    case ExecutionStatus::running:   return "running";
    case ExecutionStatus::succeeded: return "succeeded";
    case ExecutionStatus::failed:    return "failed";
    case ExecutionStatus::skipped:   return "skipped";
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
                {"boundary", base::core::to_json(plan.boundary)},
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

} // namespace ben_gear::agent::runtime
