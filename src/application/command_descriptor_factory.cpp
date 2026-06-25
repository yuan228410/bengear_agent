#include "ben_gear/application/command_descriptor_factory.hpp"

#include <string>

namespace ben_gear::application {

namespace {

container::String to_string(std::string_view value) {
    return container::String(value.data(), value.size());
}

} // namespace

CommandDescriptorFactory::CommandDescriptorFactory(RequestContext request, container::String project_path)
    : request_(std::move(request)), project_path_(std::move(project_path)) {}

CommandDescriptor CommandDescriptorFactory::make(std::string_view action) const {
    CommandDescriptor command;
    command.action = to_string(action);
    command.username = request_.username;
    command.workspace_name = request_.workspace_name;
    command.session_id = request_.session_id;
    command.project_path = project_path_;
    return command;
}

CommandDescriptor CommandDescriptorFactory::test_run(std::string_view command_text,
                                                     std::string_view cwd,
                                                     int timeout_seconds,
                                                     int max_output_bytes) const {
    auto command = make("test.run");
    command.subject = to_string(command_text);
    command.risk = CommandRisk::command_execution;
    command.runs_command = true;
    command.timeout_seconds = timeout_seconds;
    command.max_output_bytes = max_output_bytes;
    command.working_directory = to_string(cwd);
    return command;
}


CommandDescriptor CommandDescriptorFactory::patch_apply(const std::vector<std::string>& paths) const {
    auto command = make("patch.apply");
    command.risk = CommandRisk::workspace_write;
    command.mutates_workspace = true;
    add_paths(command, paths);
    return command;
}

CommandDescriptor CommandDescriptorFactory::patch_revert(std::string_view change_id,
                                                         const std::vector<std::string>& paths,
                                                         bool force) const {
    auto command = make("patch.revert");
    command.subject = to_string(change_id);
    command.risk = force ? CommandRisk::destructive : CommandRisk::workspace_write;
    command.mutates_workspace = true;
    command.force = force;
    add_paths(command, paths);
    return command;
}

CommandDescriptor CommandDescriptorFactory::checkpoint_restore(std::string_view checkpoint_id,
                                                               const std::vector<std::string>& paths,
                                                               bool force) const {
    auto command = make("checkpoint.restore");
    command.subject = to_string(checkpoint_id);
    command.risk = force ? CommandRisk::destructive : CommandRisk::workspace_write;
    command.mutates_workspace = true;
    command.force = force;
    add_paths(command, paths);
    return command;
}

CommandDescriptor CommandDescriptorFactory::checkpoint_delete(std::string_view checkpoint_id) const {
    auto command = make("checkpoint.delete");
    command.subject = to_string(checkpoint_id);
    command.risk = CommandRisk::destructive;
    return command;
}

CommandDescriptor CommandDescriptorFactory::git_branch(std::string_view action,
                                                       std::string_view name,
                                                       bool force) const {
    auto command = make(std::string("git.branch.") + std::string(action));
    command.subject = to_string(name);
    command.risk = (action == "delete" || force) ? CommandRisk::destructive : CommandRisk::workspace_write;
    command.runs_command = true;
    command.force = force;
    return command;
}

CommandDescriptor CommandDescriptorFactory::git_commit(std::string_view message,
                                                       const std::vector<std::string>& paths,
                                                       bool all,
                                                       bool amend) const {
    auto command = make("git.commit");
    command.subject = to_string(message);
    command.risk = amend ? CommandRisk::destructive : CommandRisk::workspace_write;
    command.runs_command = true;
    command.all = all;
    command.amend = amend;
    add_paths(command, paths);
    return command;
}

CommandDescriptor CommandDescriptorFactory::git_restore(const std::vector<std::string>& paths,
                                                        bool staged,
                                                        bool worktree) const {
    auto command = make("git.restore");
    command.risk = CommandRisk::workspace_write;
    command.mutates_workspace = worktree;
    command.runs_command = true;
    command.staged = staged;
    command.worktree = worktree;
    add_paths(command, paths);
    return command;
}

CommandDescriptor CommandDescriptorFactory::git_worktree(std::string_view action,
                                                         std::string_view location,
                                                         std::string_view branch,
                                                         bool create_branch,
                                                         bool force) const {
    auto command = make(std::string("git.worktree.") + std::string(action));
    command.subject = to_string(location.empty() ? branch : location);
    command.risk = (action == "remove" || force) ? CommandRisk::destructive : CommandRisk::workspace_write;
    command.runs_command = true;
    command.force = force;
    command.worktree = true;
    command.create_branch = create_branch;
    if (!location.empty()) command.affected_paths.push_back(to_string(location));
    return command;
}

void CommandDescriptorFactory::add_paths(CommandDescriptor& command, const std::vector<std::string>& paths) const {
    for (const auto& path : paths) command.affected_paths.push_back(container::String(path.c_str()));
}

} // namespace ben_gear::application
