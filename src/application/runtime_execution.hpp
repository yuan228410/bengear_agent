#pragma once

#include "application/command.hpp"
#include "base/container/string.hpp"
#include "base/utils/json.hpp"
#include "base/core/runtime_boundary.hpp"
#include "domain/result.hpp"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace ben_gear::application {

namespace container = base::container;

enum class ExecutionStepKind {
    validate,
    authorize,
    checkpoint,
    execute,
    audit,
};

enum class ExecutionStatus {
    planned,
    running,
    succeeded,
    failed,
    skipped,
};

struct ExecutionStep {
    container::String step_id;
    ExecutionStepKind kind = ExecutionStepKind::execute;
    container::String title;
    bool required = true;
    bool mutates_workspace = false;
    Json metadata = Json::object();
};

struct ExecutionPlan {
    container::String plan_id;
    core::RuntimeBoundary boundary;
    std::vector<ExecutionStep> steps;
    bool dry_run = false;
};

struct ExecutionTraceEvent {
    container::String step_id;
    ExecutionStepKind kind = ExecutionStepKind::execute;
    ExecutionStatus status = ExecutionStatus::planned;
    container::String error_type;
    container::String message;
    Json details = Json::object();
};

struct ExecutionRequest {
    container::String request_id;
    CommandDescriptor command;
    core::RuntimeBoundary boundary;
    bool dry_run = false;
};

struct ExecutionResult {
    container::String request_id;
    ExecutionStatus status = ExecutionStatus::planned;
    ExecutionPlan plan;
    std::vector<ExecutionTraceEvent> trace;
    Json output = Json::object();
};

struct RuntimeExecutionHooks {
    using ValidateHook = std::function<domain::AppResult<void>(const ExecutionRequest&, const ExecutionPlan&)>;
    using AuthorizeHook = std::function<domain::AppResult<void>(const ExecutionRequest&, const ExecutionPlan&)>;
    using CheckpointHook = std::function<domain::AppResult<void>(const ExecutionRequest&, const ExecutionPlan&)>;
    using ExecuteHook = std::function<domain::AppResult<Json>(const ExecutionRequest&, const ExecutionPlan&)>;
    using AuditHook = std::function<void(const ExecutionRequest&, const ExecutionResult&)>;

    RuntimeExecutionHooks(ValidateHook validate_hook = {},
                          AuthorizeHook authorize_hook = {},
                          CheckpointHook checkpoint_hook = {},
                          ExecuteHook execute_hook = {},
                          AuditHook audit_hook = {},
                          core::RuntimeEventSink runtime_event_sink = {})
        : validate(std::move(validate_hook)),
          authorize(std::move(authorize_hook)),
          checkpoint(std::move(checkpoint_hook)),
          execute(std::move(execute_hook)),
          audit(std::move(audit_hook)),
          event_sink(std::move(runtime_event_sink)) {}

    ValidateHook validate;
    AuthorizeHook authorize;
    CheckpointHook checkpoint;
    ExecuteHook execute;
    AuditHook audit;
    core::RuntimeEventSink event_sink;
};

std::string to_string(ExecutionStepKind kind);
std::string to_string(ExecutionStatus status);
Json to_json(const ExecutionStep& step);
Json to_json(const ExecutionPlan& plan);
Json to_json(const ExecutionTraceEvent& event);
Json to_json(const ExecutionResult& result);

ExecutionPlan make_execution_plan(const ExecutionRequest& request);

class RuntimeExecutionKernel {
public:
    explicit RuntimeExecutionKernel(RuntimeExecutionHooks hooks = {});

    ExecutionPlan plan(const ExecutionRequest& request) const;
    ExecutionResult execute(const ExecutionRequest& request) const;

private:
    static domain::AppResult<void> run_void_stage(
        const std::function<domain::AppResult<void>(const ExecutionRequest&, const ExecutionPlan&)>& stage,
        const ExecutionRequest& request,
        const ExecutionPlan& plan);

    RuntimeExecutionHooks hooks_;
};

} // namespace ben_gear::application
