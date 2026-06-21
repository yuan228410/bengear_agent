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

CommandPipeline make_command_pipeline(CommandGovernanceConfig config) {
    return CommandPipeline(CommandPipelineHooks{
        {},
        [check_permission = std::move(config.check_permission)](const CommandDescriptor& command) {
            auto tool_name = command_tool_name(command);
            if (tool_name.empty()) {
                return domain::AppResult<void>::failure(
                    domain::AppError::invalid_argument(container::String("unknown_command"), command.action));
            }

            auto decision = check_permission(command.workspace_name,
                                             command.session_id,
                                             command.username,
                                             tool_name,
                                             command_permission_arguments(command));
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
        [append_audit_event = std::move(config.append_audit_event)](const CommandDescriptor& command, const domain::AppError* error) {
            if (!append_audit_event) return;
            append_audit_event(command.workspace_name,
                               command.session_id,
                               command.username,
                               "command",
                               std::string(command.action.c_str()),
                               Json{{"command", std::string(command.action.c_str())},
                                    {"risk", command_risk_name(command.risk)},
                                    {"outcome", error ? "failed" : "success"},
                                    {"error_type", error ? std::string(error->code.c_str()) : std::string()},
                                    {"subject", std::string(command.subject.c_str())},
                                    {"paths", command_paths_json(command)}});
        }});
}

} // namespace ben_gear::application
