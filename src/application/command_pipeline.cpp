#include "application/command_pipeline.hpp"

#include <string>

namespace ben_gear::application {


namespace {

domain::AppError categorized_error(std::string code, std::string message, std::string details) {
    domain::AppError error = code.find("permission") != std::string::npos
                                 ? domain::AppError::permission_denied(std::string(code), std::string(message))
                                 : domain::AppError::internal(std::string(code), std::string(message));
    if (!details.empty()) error.details_json = std::string(details);
    return error;
}

} // namespace

core::MutationScope command_mutation_scope(CommandRisk risk) {
    switch (risk) {
    case CommandRisk::read_only: return core::MutationScope::none;
    case CommandRisk::workspace_read: return core::MutationScope::workspace_read;
    case CommandRisk::workspace_write: return core::MutationScope::workspace_write;
    case CommandRisk::command_execution: return core::MutationScope::workspace_write;
    case CommandRisk::destructive: return core::MutationScope::repository_write;
    }
    return core::MutationScope::none;
}

core::RuntimeCapability command_runtime_capability(const CommandDescriptor& command) {
    const auto action = command.action;
    if (action == "patch.apply") return core::RuntimeCapability::patch_apply;
    if (action == "patch.revert") return core::RuntimeCapability::patch_apply;
    if (action == "patch.preview") return core::RuntimeCapability::patch_preview;
    if (action == "diff.read") return core::RuntimeCapability::diff_read;
    if (action == "git.status") return core::RuntimeCapability::git_status;
    if (action == "git.commit") return core::RuntimeCapability::git_commit;
    if (action.rfind("git.", 0) == 0) return core::RuntimeCapability::git_status;
    if (action.rfind("checkpoint.", 0) == 0) {
        return action == "checkpoint.restore" ? core::RuntimeCapability::checkpoint_restore
                                               : core::RuntimeCapability::checkpoint_create;
    }
    if (action == "test.run") return core::RuntimeCapability::test_loop;
    if (action.rfind("repo_map.", 0) == 0) return core::RuntimeCapability::repo_map;
    if (action.rfind("code_intel.", 0) == 0) return core::RuntimeCapability::code_intel;
    return core::RuntimeCapability::tool_call;
}

core::RuntimeOperation to_runtime_operation(const CommandDescriptor& command) {
    core::RuntimeOperation operation;
    operation.operation_id = command.action;
    operation.capability = command_runtime_capability(command);
    operation.scope = command_mutation_scope(command.risk);
    operation.actor = command.username;
    operation.description = command.subject;
    operation.workspace.username = command.username;
    operation.workspace.workspace_name = command.workspace_name;
    operation.workspace.project_path = command.project_path;
    operation.workspace.session_id = command.session_id;
    return operation;
}


CommandPipeline::CommandPipeline(CommandPipelineHooks hooks)
    : hooks_(std::move(hooks)) {}

domain::AppResult<void> CommandPipeline::run_stage(
    const std::function<domain::AppResult<void>(const CommandDescriptor&)>& stage,
    const CommandDescriptor& descriptor) {
    if (!stage) return domain::AppResult<void>::success();
    return stage(descriptor);
}

void CommandPipeline::audit(const CommandDescriptor& descriptor, const domain::AppError* error) const {
    if (!hooks_.audit) return;
    hooks_.audit(descriptor, error);
}


domain::AppError CommandPipeline::error_from_execution(const ExecutionResult& result) {
    std::string code = result.output.value("error_type", "execution_failed");
    std::string message = result.output.value("message", code);
    std::string details;
    if (result.output.contains("details")) details = result.output["details"].dump();
    if (details.empty()) details = to_json(result).dump();
    return categorized_error(code, message, details);
}

void CommandPipeline::audit_runtime(const CommandDescriptor& descriptor, const ExecutionResult& result) const {
    if (hooks_.runtime_audit) {
        hooks_.runtime_audit(descriptor, result);
        return;
    }
    auto error = result.status == ExecutionStatus::succeeded ? domain::AppError{} : error_from_execution(result);
    audit(descriptor, result.status == ExecutionStatus::succeeded ? nullptr : &error);
}

} // namespace ben_gear::application
