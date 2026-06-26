#include "ben_gear/application/command_governance.hpp"

#include "ben_gear/domain/errors.hpp"

#include <utility>
#include <vector>

namespace ben_gear::application {

namespace {

std::string command_action_suffix(const CommandDescriptor& command, std::string_view prefix) {
    auto action = std::string(command.action.c_str());
    auto prefix_text = std::string(prefix);
    if (action.rfind(prefix_text, 0) == 0 && action.size() > prefix_text.size()) return action.substr(prefix_text.size());
    return action;
}

} // namespace

Json command_paths_json(const CommandDescriptor& command) {
    Json paths = Json::array();
    for (const auto& path : command.affected_paths) paths.push_back(std::string(path.c_str()));
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
    auto action = std::string(command.action.c_str());
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
    auto action = std::string(command.action.c_str());
    if (action.rfind("git.branch.", 0) == 0) {
        return Json{{"action", command_action_suffix(command, "git.branch.")},
                    {"name", std::string(command.subject.c_str())},
                    {"force", command.force},
                    {"project_path", std::string(command.project_path.c_str())}};
    }
    if (action == "git.restore") {
        return Json{{"paths", command_paths_json(command)},
                    {"staged", command.staged},
                    {"worktree", command.worktree},
                    {"project_path", std::string(command.project_path.c_str())}};
    }
    if (action == "git.commit") {
        return Json{{"message", std::string(command.subject.c_str())},
                    {"paths", command_paths_json(command)},
                    {"all", command.all},
                    {"amend", command.amend},
                    {"project_path", std::string(command.project_path.c_str())}};
    }
    if (action == "checkpoint.restore") {
        return Json{{"checkpoint_id", std::string(command.subject.c_str())},
                    {"paths", command_paths_json(command)},
                    {"force", command.force},
                    {"project_path", std::string(command.project_path.c_str())}};
    }
    if (action == "checkpoint.delete") {
        return Json{{"checkpoint_id", std::string(command.subject.c_str())},
                    {"project_path", std::string(command.project_path.c_str())}};
    }
    if (action.rfind("git.worktree.", 0) == 0) {
        return Json{{"action", command_action_suffix(command, "git.worktree.")},
                    {"location", std::string(command.subject.c_str())},
                    {"paths", command_paths_json(command)},
                    {"create_branch", command.create_branch},
                    {"force", command.force},
                    {"project_path", std::string(command.project_path.c_str())}};
    }
    if (action == "test.run") {
        return Json{{"command", std::string(command.subject.c_str())},
                    {"cwd", std::string(command.working_directory.c_str())},
                    {"timeout_seconds", command.timeout_seconds},
                    {"max_output_bytes", command.max_output_bytes},
                    {"project_path", std::string(command.project_path.c_str())}};
    }
    return Json{{"paths", command_paths_json(command)},
                {"project_path", std::string(command.project_path.c_str())}};
}


core::PermissionGateRef command_permission_gate(const CommandDescriptor& command) {
    core::PermissionGateRef gate;
    gate.permission_id = command.action;
    gate.policy_key = container::String(command_tool_name(command).c_str());
    gate.requested_scope = command_mutation_scope(command.risk);
    gate.resource = command_permission_arguments(command);
    return gate;
}

core::RuntimeBoundary command_runtime_boundary(const CommandDescriptor& command) {
    core::RuntimeBoundary boundary;
    boundary.operation = to_runtime_operation(command);
    if (auto tool_name = command_tool_name(command); !tool_name.empty()) {
        boundary.tool_calls.push_back(core::ToolCallRef{command.action,
                                                       container::String(tool_name.c_str()),
                                                       command_permission_arguments(command)});
        boundary.permission_gates.push_back(command_permission_gate(command));
    }
    for (const auto& path : command.affected_paths) {
        boundary.diffs.push_back(core::DiffRef{path, 0, 0});
    }
    const auto action = std::string(command.action.c_str());
    if (action.rfind("git.", 0) == 0) {
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


ExecutionRequest command_execution_request(const CommandDescriptor& command, bool dry_run) {
    ExecutionRequest request;
    request.request_id = command.action;
    request.command = command;
    request.boundary = command_runtime_boundary(command);
    request.dry_run = dry_run;
    return request;
}

RuntimeExecutionKernel make_runtime_execution_kernel(CommandGovernanceConfig config) {
    return RuntimeExecutionKernel(RuntimeExecutionHooks{
        {},
        [check_permission = std::move(config.check_permission)](const ExecutionRequest& request, const ExecutionPlan&) {
            const auto& command = request.command;
            auto tool_name = command_tool_name(command);
            if (tool_name.empty()) {
                return domain::AppResult<void>::failure(
                    domain::AppError::invalid_argument(container::String("unknown_command"), command.action));
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
                container::String(decision.value("error_type", "permission_denied").c_str()),
                container::String(decision.value("message", "permission denied").c_str()));
            error.details_json = decision.dump();
            return domain::AppResult<void>::failure(std::move(error));
        },
        [create_checkpoint = std::move(config.create_checkpoint)](const ExecutionRequest& request, const ExecutionPlan&) {
            if (!create_checkpoint) return domain::AppResult<void>::success();
            return create_checkpoint(request.command);
        },
        {},
        [append_audit_event = std::move(config.append_audit_event)](const ExecutionRequest& request, const ExecutionResult& result) {
            if (!append_audit_event) return;
            const auto& command = request.command;
            append_audit_event(command.workspace_name,
                               command.session_id,
                               command.username,
                               "runtime_execution",
                               std::string(command.action.c_str()),
                               Json{{"command", std::string(command.action.c_str())},
                                    {"execution", to_json(result)},
                                    {"runtime_boundary", core::to_json(request.boundary)},
                                    {"risk", command_risk_name(command.risk)},
                                    {"outcome", to_string(result.status)},
                                    {"subject", std::string(command.subject.c_str())},
                                    {"paths", command_paths_json(command)}});
        }});
}

CommandPipeline make_command_pipeline(CommandGovernanceConfig config) {
    return CommandPipeline(CommandPipelineHooks{
        {},
        [check_permission = std::move(config.check_permission)](const CommandDescriptor& command) {
            auto tool_name = command_tool_name(command);
            if (tool_name.empty()) {
                return domain::AppResult<void>::failure(
                    domain::AppError::invalid_argument(container::String("unknown_command"), command.action));
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
                container::String(decision.value("error_type", "permission_denied").c_str()),
                container::String(decision.value("message", "permission denied").c_str()));
            error.details_json = decision.dump();
            return domain::AppResult<void>::failure(std::move(error));
        },
        [create_checkpoint = std::move(config.create_checkpoint)](const CommandDescriptor& command) {
            if (!create_checkpoint) return domain::AppResult<void>::success();
            return create_checkpoint(command);
        },
        {},
        [append_audit_event = std::move(config.append_audit_event)](const CommandDescriptor& command, const ExecutionResult& result) {
            if (!append_audit_event) return;
            append_audit_event(command.workspace_name,
                               command.session_id,
                               command.username,
                               "runtime_execution",
                               std::string(command.action.c_str()),
                               Json{{"command", std::string(command.action.c_str())},
                                    {"execution", to_json(result)},
                                    {"runtime_boundary", core::to_json(command_runtime_boundary(command))},
                                    {"risk", command_risk_name(command.risk)},
                                    {"outcome", result.status == ExecutionStatus::succeeded ? "success" : "failed"},
                                    {"execution_status", to_string(result.status)},
                                    {"error_type", result.output.value("error_type", "")},
                                    {"subject", std::string(command.subject.c_str())},
                                    {"paths", command_paths_json(command)}});
        }});
}

} // namespace ben_gear::application
