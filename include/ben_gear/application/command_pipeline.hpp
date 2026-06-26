#pragma once

#include "ben_gear/application/command.hpp"
#include "ben_gear/application/runtime_execution.hpp"
#include "ben_gear/domain/result.hpp"

#include <functional>
#include <optional>
#include <type_traits>
#include <utility>

namespace ben_gear::application {

struct CommandPipelineHooks {
    std::function<domain::AppResult<void>(const CommandDescriptor&)> validate;
    std::function<domain::AppResult<void>(const CommandDescriptor&)> authorize;
    std::function<domain::AppResult<void>(const CommandDescriptor&)> checkpoint;
    std::function<void(const CommandDescriptor&, const domain::AppError*)> audit;
    std::function<void(const CommandDescriptor&, const ExecutionResult&)> runtime_audit;
};

class CommandPipeline {
public:
    explicit CommandPipeline(CommandPipelineHooks hooks = {});

    template <class T, class Handler>
    domain::AppResult<T> execute(const CommandDescriptor& descriptor, Handler&& handler) const {
        std::optional<domain::AppResult<T>> handler_result;
        ExecutionRequest request;
        request.request_id = descriptor.action;
        request.command = descriptor;
        request.boundary.operation = to_runtime_operation(descriptor);
        RuntimeExecutionKernel kernel(RuntimeExecutionHooks{
            [this](const ExecutionRequest& request, const ExecutionPlan&) {
                return run_stage(hooks_.validate, request.command);
            },
            [this](const ExecutionRequest& request, const ExecutionPlan&) {
                return run_stage(hooks_.authorize, request.command);
            },
            [this](const ExecutionRequest& request, const ExecutionPlan&) {
                return run_stage(hooks_.checkpoint, request.command);
            },
            [&](const ExecutionRequest&, const ExecutionPlan&) {
                handler_result = std::forward<Handler>(handler)();
                if (!handler_result->ok()) return domain::AppResult<Json>::failure(handler_result->error());
                if constexpr (std::is_same_v<T, Json>) {
                    return domain::AppResult<Json>::success(handler_result->value());
                }
                return domain::AppResult<Json>::success(Json{{"success", true}});
            },
            [this](const ExecutionRequest& request, const ExecutionResult& result) {
                audit_runtime(request.command, result);
            }});

        auto execution = kernel.execute(request);
        if (handler_result.has_value()) return std::move(*handler_result);
        if (execution.status == ExecutionStatus::succeeded) {
            return domain::AppResult<T>::failure(domain::AppError::internal(container::String("missing_result"), container::String("execution completed without handler result")));
        }
        return domain::AppResult<T>::failure(error_from_execution(execution));
    }

    template <class Handler>
    domain::AppResult<void> execute_void(const CommandDescriptor& descriptor, Handler&& handler) const {
        std::optional<domain::AppResult<void>> handler_result;
        ExecutionRequest request;
        request.request_id = descriptor.action;
        request.command = descriptor;
        request.boundary.operation = to_runtime_operation(descriptor);
        RuntimeExecutionKernel kernel(RuntimeExecutionHooks{
            [this](const ExecutionRequest& request, const ExecutionPlan&) {
                return run_stage(hooks_.validate, request.command);
            },
            [this](const ExecutionRequest& request, const ExecutionPlan&) {
                return run_stage(hooks_.authorize, request.command);
            },
            [this](const ExecutionRequest& request, const ExecutionPlan&) {
                return run_stage(hooks_.checkpoint, request.command);
            },
            [&](const ExecutionRequest&, const ExecutionPlan&) {
                handler_result = std::forward<Handler>(handler)();
                if (!handler_result->ok()) return domain::AppResult<Json>::failure(handler_result->error());
                return domain::AppResult<Json>::success(Json{{"success", true}});
            },
            [this](const ExecutionRequest& request, const ExecutionResult& result) {
                audit_runtime(request.command, result);
            }});

        auto execution = kernel.execute(request);
        if (handler_result.has_value()) return std::move(*handler_result);
        if (execution.status == ExecutionStatus::succeeded) {
            return domain::AppResult<void>::failure(domain::AppError::internal(container::String("missing_result"), container::String("execution completed without handler result")));
        }
        return domain::AppResult<void>::failure(error_from_execution(execution));
    }

private:
    static domain::AppResult<void> run_stage(
        const std::function<domain::AppResult<void>(const CommandDescriptor&)>& stage,
        const CommandDescriptor& descriptor);

    void audit(const CommandDescriptor& descriptor, const domain::AppError* error) const;
    void audit_runtime(const CommandDescriptor& descriptor, const ExecutionResult& result) const;
    static domain::AppError error_from_execution(const ExecutionResult& result);

    CommandPipelineHooks hooks_;
};

} // namespace ben_gear::application
