#include "agent/runtime/application/command_governance.hpp"

#include "domain/errors.hpp"

#include <utility>
#include <vector>

namespace ben_gear::application {

namespace {

std::string command_action_suffix(const CommandDescriptor& command, std::string_view prefix) {
    auto action = command.action;
    auto prefix_text = std::string(prefix);
    if (action.rfind(prefix_text, 0) == 0 && action.size() > prefix_text.size()) return action.substr(prefix_text.size());
    return action;
}

} // namespace

Json command_paths_json(const CommandDescriptor& command) {
    Json paths = Json::array();
    for (const auto& path : command.affected_paths) paths.push_back(path);
    return paths;
}

std::string command_risk_name(CommandRisk risk) {
    switch (risk) {
        case CommandRisk::read_only: return "read_only";
        case CommandRisk::workspace_read: return "workspace_read";
        case CommandRisk::workspace_write: return "workspace_write";
        case CommandRisk::command_execution: return "command_execution";
        case CommandRisk::destructive: return "destructive";
    }
    return "unknown";
}

std::string command_tool_name(const CommandDescriptor& command) {
    auto action = command.action;
    if (action == "safe_code_change.run") return "safe_code_change";
    if (action == "patch.apply") return "apply_patch";
    if (action == "patch.revert") return "revert_patch";
    if (action == "test.run") return "run_tests";
    if (action == "git.restore") return "git_restore";
    if (action == "git.commit") return "git_commit";
    if (action.rfind("git.branch.", 0) == 0) return "git_branch";
    if (action.rfind("git.worktree.", 0) == 0) return "git_worktree";
    if (action == "checkpoint.restore") return "restore_checkpoint";
    if (action == "checkpoint.delete") return "delete_checkpoint";
    return {};
}

Json command_permission_arguments(const CommandDescriptor& command) {
    auto action = command.action;
    if (action == "safe_code_change.run") {
        return Json{{"paths", command_paths_json(command)},
                    {"description", command.subject},
                    {"test_command", command.working_directory},
                    {"timeout_seconds", command.timeout_seconds},
                    {"max_output_bytes", command.max_output_bytes},
                    {"project_path", command.project_path}};
    }
    if (action.rfind("git.branch.", 0) == 0) {
        return Json{{"action", command_action_suffix(command, "git.branch.")},
                    {"name", command.subject},
                    {"force", command.force},
                    {"project_path", command.project_path}};
    }
    if (action == "git.restore") {
        return Json{{"paths", command_paths_json(command)},
                    {"staged", command.staged},
                    {"worktree", command.worktree},
                    {"project_path", command.project_path}};
    }
    if (action == "git.commit") {
        return Json{{"message", command.subject},
                    {"paths", command_paths_json(command)},
                    {"all", command.all},
                    {"amend", command.amend},
                    {"project_path", command.project_path}};
    }
    if (action == "checkpoint.restore") {
        return Json{{"checkpoint_id", command.subject},
                    {"paths", command_paths_json(command)},
                    {"force", command.force},
                    {"project_path", command.project_path}};
    }
    if (action == "checkpoint.delete") {
        return Json{{"checkpoint_id", command.subject},
                    {"project_path", command.project_path}};
    }
    if (action.rfind("git.worktree.", 0) == 0) {
        return Json{{"action", command_action_suffix(command, "git.worktree.")},
                    {"location", command.subject},
                    {"paths", command_paths_json(command)},
                    {"create_branch", command.create_branch},
                    {"force", command.force},
                    {"project_path", command.project_path}};
    }
    if (action == "test.run") {
        return Json{{"command", command.subject},
                    {"cwd", command.working_directory},
                    {"timeout_seconds", command.timeout_seconds},
                    {"max_output_bytes", command.max_output_bytes},
                    {"project_path", command.project_path}};
    }
    return Json{{"paths", command_paths_json(command)},
                {"project_path", command.project_path}};
}


core::PermissionGateRef command_permission_gate(const CommandDescriptor& command) {
    core::PermissionGateRef gate;
    gate.permission_id = command.action;
    gate.policy_key = command_tool_name(command);
    gate.requested_scope = command_mutation_scope(command.risk);
    gate.resource = command_permission_arguments(command);
    return gate;
}

core::RuntimeBoundary command_runtime_boundary(const CommandDescriptor& command) {
    core::RuntimeBoundary boundary;
    boundary.operation = to_runtime_operation(command);
    if (auto tool_name = command_tool_name(command); !tool_name.empty()) {
        boundary.tool_calls.push_back(core::ToolCallRef{command.action,
                                                       tool_name,
                                                       command_permission_arguments(command)});
        boundary.permission_gates.push_back(command_permission_gate(command));
    }
    for (const auto& path : command.affected_paths) {
        boundary.diffs.push_back(core::DiffRef{path, 0, 0});
    }
    const auto action = command.action;
    if (action.rfind("git.", 0) == 0 || action == "safe_code_change.run") {
        boundary.git_refs.push_back(core::GitRef{command.project_path, {}, {}, true});
    }
    if (action.rfind("checkpoint.", 0) == 0) {
        boundary.checkpoints.push_back(core::CheckpointRef{command.subject, {}, 0});
    }
    if (action.rfind("repo_map.", 0) == 0) {
        boundary.repo_maps.push_back(core::RepoMapRef{command.project_path, 0, 0});
    }
    return boundary;
}


CommandDescriptor safe_code_change_command(const CommandDescriptor& patch_command,
                                           std::string_view test_command,
                                           std::string_view test_cwd,
                                           int test_timeout_seconds,
                                           int test_max_output_bytes) {
    auto command = patch_command;
    command.action = std::string("safe_code_change.run");
    command.risk = CommandRisk::command_execution;
    command.mutates_workspace = true;
    command.runs_command = true;
    command.working_directory = std::string(test_command.data(), test_command.size());
    command.timeout_seconds = test_timeout_seconds;
    command.max_output_bytes = test_max_output_bytes;
    command.subject = std::string(test_cwd.data(), test_cwd.size());
    return command;
}

ExecutionRequest command_execution_request(const CommandDescriptor& command, bool dry_run) {
    ExecutionRequest request;
    request.request_id = command.action;
    request.command = command;
    request.boundary = command_runtime_boundary(command);
    request.dry_run = dry_run;
    return request;
}

Json runtime_execution_record(const CommandDescriptor& command,
                              const ExecutionRequest& request,
                              const ExecutionResult& result,
                              const Json& audit_result) {
    Json record{{"workspace", command.workspace_name},
                {"session_id", command.session_id},
                {"username", command.username},
                {"request_id", result.request_id},
                {"action", command.action},
                {"status", to_string(result.status)},
                {"operation", core::to_json(request.boundary.operation)},
                {"runtime_boundary", core::to_json(request.boundary)},
                {"risk", command_risk_name(command.risk)},
                {"subject", command.subject},
                {"paths", command_paths_json(command)},
                {"execution", to_json(result)}};
    if (audit_result.value("success", false) && audit_result.contains("event")) {
        record["audit_event_id"] = audit_result["event"].value("event_id", "");
    }
    return record;
}


RuntimeExecutionKernel make_runtime_execution_kernel(CommandGovernanceConfig config) {
    return RuntimeExecutionKernel(RuntimeExecutionHooks{
        {},
        [check_permission = std::move(config.check_permission)](const ExecutionRequest& request, const ExecutionPlan&) {
            const auto& command = request.command;
            auto tool_name = command_tool_name(command);
            if (tool_name.empty()) {
                return domain::AppResult<void>::failure(
                    domain::AppError::invalid_argument(std::string("unknown_command"), command.action));
            }

            auto args = command_permission_arguments(command);
            args["runtime_boundary"] = core::to_json(request.boundary);
            args["runtime_operation"] = core::to_json(request.boundary.operation);
            args["permission_gate"] = core::to_json(command_permission_gate(command));
            auto decision = check_permission(command.workspace_name,
                                             command.session_id,
                                             command.username,
                                             tool_name,
                                             args);
            auto allowed = decision.value("success", false) || std::string(decision.value("policy_effect", "")) == "allow";
            if (allowed) return domain::AppResult<void>::success();

            auto error = domain::AppError::permission_denied(
                decision.value("error_type", "permission_denied"),
                decision.value("message", "permission denied"));
            error.details_json = decision.dump();
            return domain::AppResult<void>::failure(std::move(error));
        },
        [create_checkpoint = std::move(config.create_checkpoint)](const ExecutionRequest& request, const ExecutionPlan&) {
            if (!create_checkpoint) return domain::AppResult<void>::success();
            return create_checkpoint(request.command);
        },
        {},
        [append_audit_event = std::move(config.append_audit_event),
         append_runtime_execution = std::move(config.append_runtime_execution)](const ExecutionRequest& request, const ExecutionResult& result) {
            if (!append_audit_event && !append_runtime_execution) return;
            const auto& command = request.command;
            Json details{{"command", command.action},
                         {"execution", to_json(result)},
                         {"runtime_boundary", core::to_json(request.boundary)},
                         {"risk", command_risk_name(command.risk)},
                         {"outcome", to_string(result.status)},
                         {"subject", command.subject},
                         {"paths", command_paths_json(command)}};
            Json audit_result = append_audit_event ? append_audit_event(command.workspace_name,
                                                                        command.session_id,
                                                                        command.username,
                                                                        "runtime_execution",
                                                                        command.action,
                                                                        details)
                                                : Json{{"success", false}};
            if (append_runtime_execution) {
                (void)append_runtime_execution(command.workspace_name,
                                               command.session_id,
                                               command.username,
                                               runtime_execution_record(command, request, result, audit_result));
            }
        }});
}



CommandPipeline make_command_pipeline(CommandGovernanceConfig config) {
    return CommandPipeline(CommandPipelineHooks{
        {},
        [check_permission = std::move(config.check_permission)](const CommandDescriptor& command) {
            auto tool_name = command_tool_name(command);
            if (tool_name.empty()) {
                return domain::AppResult<void>::failure(
                    domain::AppError::invalid_argument(std::string("unknown_command"), command.action));
            }

            auto args = command_permission_arguments(command);
            args["runtime_operation"] = core::to_json(to_runtime_operation(command));
            args["permission_gate"] = core::to_json(command_permission_gate(command));
            auto decision = check_permission(command.workspace_name,
                                             command.session_id,
                                             command.username,
                                             tool_name,
                                             args);
            auto allowed = decision.value("success", false) || std::string(decision.value("policy_effect", "")) == "allow";
            if (allowed) return domain::AppResult<void>::success();

            auto error = domain::AppError::permission_denied(
                decision.value("error_type", "permission_denied"),
                decision.value("message", "permission denied"));
            error.details_json = decision.dump();
            return domain::AppResult<void>::failure(std::move(error));
        },
        [create_checkpoint = std::move(config.create_checkpoint)](const CommandDescriptor& command) {
            if (!create_checkpoint) return domain::AppResult<void>::success();
            return create_checkpoint(command);
        },
        {},
        [append_audit_event = std::move(config.append_audit_event),
         append_runtime_execution = std::move(config.append_runtime_execution)](const CommandDescriptor& command, const ExecutionResult& result) {
            if (!append_audit_event && !append_runtime_execution) return;
            auto request = command_execution_request(command);
            Json details{{"command", command.action},
                         {"execution", to_json(result)},
                         {"runtime_boundary", core::to_json(command_runtime_boundary(command))},
                         {"risk", command_risk_name(command.risk)},
                         {"outcome", result.status == ExecutionStatus::succeeded ? "success" : "failed"},
                         {"execution_status", to_string(result.status)},
                         {"error_type", result.output.value("error_type", "")},
                         {"subject", command.subject},
                         {"paths", command_paths_json(command)}};
            Json audit_result = append_audit_event ? append_audit_event(command.workspace_name,
                                                                        command.session_id,
                                                                        command.username,
                                                                        "runtime_execution",
                                                                        command.action,
                                                                        details)
                                                : Json{{"success", false}};
            if (append_runtime_execution) {
                (void)append_runtime_execution(command.workspace_name,
                                               command.session_id,
                                               command.username,
                                               runtime_execution_record(command, request, result, audit_result));
            }
        }});
}

} // namespace ben_gear::application
