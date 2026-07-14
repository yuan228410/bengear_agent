#include "capabilities/tool/git_tools.hpp"

#include "application/command_descriptor_factory.hpp"
#include "application/request_context.hpp"
#include "capabilities/git/git_service.hpp"
#include "capabilities/tool/registry.hpp"
#include "capabilities/tool/command_tool_helpers.hpp"

#include <memory>
#include <string>
#include <vector>

namespace ben_gear::tools {

void register_git_tools(llm::ToolRegistry& registry,
                               std::shared_ptr<git::GitService> service,
                               application::CommandPipeline command_pipeline,
                               application::RequestContext request,
                               base::container::String project_path) {
    if (!service) return;
    registry.register_tool(
        base::container::String("git_status"),
        base::container::String("Return structured git status for the current workspace."),
        {},
        [service](const Json&) -> base::container::String {
            auto result = git::to_json(service->status()).dump();
            return base::container::String(result.c_str(), result.size());
        },
        true);

    registry.register_tool(
        base::container::String("git_diff"),
        base::container::String("Return git diff for the workspace or one path. Read-only."),
        {{base::container::String("path"), {base::container::String("string"), base::container::String("Optional path to diff"), {}, false}},
         {base::container::String("staged"), {base::container::String("boolean"), base::container::String("Show staged diff"), {}, false}},
         {base::container::String("stat"), {base::container::String("boolean"), base::container::String("Show diff stat instead of patch"), {}, false}}},
        [service](const Json& args) -> base::container::String {
            auto path = args.value("path", "");
            bool staged = args.value("staged", false);
            bool stat = args.value("stat", false);
            auto result = command_detail::app_result_json(service->diff(path, staged, stat), [](const git::GitDiffResult& value) {
                return git::to_json(value);
            }).dump();
            return base::container::String(result.c_str(), result.size());
        },
        true);

    registry.register_tool(
        base::container::String("git_log"),
        base::container::String("Return structured git commit history. Read-only."),
        {{base::container::String("limit"), {base::container::String("integer"), base::container::String("Maximum commits to return, clamped to 200"), {}, false}},
         {base::container::String("path"), {base::container::String("string"), base::container::String("Optional path to filter commit history"), {}, false}}},
        [service](const Json& args) -> base::container::String {
            int limit = args.value("limit", 20);
            auto path = args.value("path", "");
            auto result = command_detail::app_result_json(service->log(limit, path), [](const git::GitLogResult& value) {
                return git::to_json(value);
            }).dump();
            return base::container::String(result.c_str(), result.size());
        },
        true);

    registry.register_tool(
        base::container::String("git_branch"),
        base::container::String("List, create, switch, or delete git branches. Mutating actions are permission-gated."),
        {{base::container::String("action"), {base::container::String("string"), base::container::String("Action: list, create, switch, delete"), {}, false}},
         {base::container::String("name"), {base::container::String("string"), base::container::String("Branch name for create/switch/delete"), {}, false}},
         {base::container::String("start_point"), {base::container::String("string"), base::container::String("Optional start point for create"), {}, false}},
         {base::container::String("force"), {base::container::String("boolean"), base::container::String("Force create/switch/delete when supported"), {}, false}}},
        [service, command_pipeline, request, project_path](const Json& args) -> base::container::String {
            auto action = args.value("action", "list");
            auto name = args.value("name", "");
            auto start_point = args.value("start_point", "");
            bool force = args.value("force", false);
            if (action == "list") {
                auto result = command_detail::app_result_json(service->list_branches(), [](const git::GitBranchListResult& value) {
                    return git::to_json(value);
                }).dump();
                return base::container::String(result.c_str(), result.size());
            }

            auto command = application::CommandDescriptorFactory(request, project_path)
                               .git_branch(action, name, force);

            return command_detail::pipeline_tool_output(command_pipeline.execute<Json>(command, [&]() {
                if (action == "create") {
                    return command_detail::presented_command_result(service->create_branch(name, start_point, force), [](const git::GitBranchMutationResult& value) {
                        return git::to_json(value);
                    });
                }
                if (action == "switch") {
                    return command_detail::presented_command_result(service->switch_branch(name, force), [](const git::GitBranchMutationResult& value) {
                        return git::to_json(value);
                    });
                }
                if (action == "delete") {
                    return command_detail::presented_command_result(service->delete_branch(name, force), [](const git::GitBranchMutationResult& value) {
                        return git::to_json(value);
                    });
                }
                return domain::AppResult<Json>::failure(domain::AppError::invalid_argument(base::container::String("invalid_arguments"), base::container::String("unsupported branch action")));
            }));
        });

    registry.register_tool(
        base::container::String("git_commit"),
        base::container::String("Create a git commit. Mutating and permission-gated."),
        {{base::container::String("message"), {base::container::String("string"), base::container::String("Commit message"), {}, true}},
         {base::container::String("paths"), {base::container::String("array"), base::container::String("Optional paths to stage before committing"), {}, false}},
         {base::container::String("all"), {base::container::String("boolean"), base::container::String("Stage tracked modifications with --all"), {}, false}},
         {base::container::String("amend"), {base::container::String("boolean"), base::container::String("Amend the previous commit"), {}, false}}},
        [service, command_pipeline, request, project_path](const Json& args) -> base::container::String {
            std::vector<std::string> paths;
            if (args.contains("paths") && args["paths"].is_array()) {
                for (const auto& item : args["paths"]) if (item.is_string()) paths.push_back(item.get<std::string>());
            }
            auto message = args.value("message", "");
            bool all = args.value("all", false);
            bool amend = args.value("amend", false);

            auto command = application::CommandDescriptorFactory(request, project_path)
                               .git_commit(message, paths, all, amend);

            return command_detail::pipeline_tool_output(command_pipeline.execute<Json>(command, [&]() {
                return command_detail::presented_command_result(service->commit(message, paths, all, amend), [](const git::GitCommitResult& value) {
                    return git::to_json(value);
                });
            }));
        });

    registry.register_tool(
        base::container::String("git_restore"),
        base::container::String("Restore tracked workspace files with git restore. Mutating and permission-gated."),
        {{base::container::String("paths"), {base::container::String("array"), base::container::String("Paths to restore; must be non-empty"), {}, true}},
         {base::container::String("staged"), {base::container::String("boolean"), base::container::String("Restore staged changes"), {}, false}},
         {base::container::String("worktree"), {base::container::String("boolean"), base::container::String("Restore worktree changes"), {}, false}}},
        [service, command_pipeline, request, project_path](const Json& args) -> base::container::String {
            std::vector<std::string> paths;
            if (args.contains("paths") && args["paths"].is_array()) {
                for (const auto& item : args["paths"]) if (item.is_string()) paths.push_back(item.get<std::string>());
            }
            bool staged = args.value("staged", false);
            bool worktree = args.value("worktree", true);

            auto command = application::CommandDescriptorFactory(request, project_path)
                               .git_restore(paths, staged, worktree);

            return command_detail::pipeline_tool_output(command_pipeline.execute<Json>(command, [&]() {
                return command_detail::presented_command_result(service->restore(paths, staged, worktree), [](const git::GitRestoreResult& value) {
                    return git::to_json(value);
                });
            }));
        });

    registry.register_tool(
        base::container::String("git_worktree"),
        base::container::String("List, add, remove, or prune git worktrees. Mutating actions are permission-gated."),
        {{base::container::String("action"), {base::container::String("string"), base::container::String("Action: list, add, remove, prune"), {}, false}},
         {base::container::String("location"), {base::container::String("string"), base::container::String("Relative worktree location for add/remove"), {}, false}},
         {base::container::String("branch"), {base::container::String("string"), base::container::String("Branch name/ref for add"), {}, false}},
         {base::container::String("create_branch"), {base::container::String("boolean"), base::container::String("Create branch with -b during add"), {}, false}},
         {base::container::String("force"), {base::container::String("boolean"), base::container::String("Force add/remove when supported"), {}, false}}},
        [service, command_pipeline, request, project_path](const Json& args) -> base::container::String {
            auto action = args.value("action", "list");
            auto location = args.value("location", "");
            auto branch = args.value("branch", "");
            bool create_branch = args.value("create_branch", false);
            bool force = args.value("force", false);
            if (action == "list") {
                auto result = command_detail::app_result_json(service->list_worktrees(), [](const git::GitWorktreeListResult& value) {
                    return git::to_json(value);
                }).dump();
                return base::container::String(result.c_str(), result.size());
            }

            auto command = application::CommandDescriptorFactory(request, project_path)
                               .git_worktree(action, location, branch, create_branch, force);

            return command_detail::pipeline_tool_output(command_pipeline.execute<Json>(command, [&]() {
                if (action == "add") {
                    return command_detail::presented_command_result(service->add_worktree(location, branch, create_branch, force), [](const git::GitWorktreeMutationResult& value) {
                        return git::to_json(value);
                    });
                }
                if (action == "remove") {
                    return command_detail::presented_command_result(service->remove_worktree(location, force), [](const git::GitWorktreeMutationResult& value) {
                        return git::to_json(value);
                    });
                }
                if (action == "prune") {
                    return command_detail::presented_command_result(service->prune_worktrees(), [](const git::GitWorktreeMutationResult& value) {
                        return git::to_json(value);
                    });
                }
                return domain::AppResult<Json>::failure(domain::AppError::invalid_argument(base::container::String("invalid_arguments"), base::container::String("unsupported worktree action")));
            }));
        });
}

} // namespace ben_gear::tools
