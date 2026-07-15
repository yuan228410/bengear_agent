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
                               std::string project_path) {
    if (!service) return;
    registry.register_tool(
        std::string("git_status"),
        std::string("Return structured git status for the current workspace."),
        {},
        [service](const Json&) -> std::string {
            auto result = git::to_json(service->status()).dump();
            return std::string(result.c_str(), result.size());
        },
        true);

    registry.register_tool(
        std::string("git_diff"),
        std::string("Return git diff for the workspace or one path. Read-only."),
        {{std::string("path"), {std::string("string"), std::string("Optional path to diff"), {}, false}},
         {std::string("staged"), {std::string("boolean"), std::string("Show staged diff"), {}, false}},
         {std::string("stat"), {std::string("boolean"), std::string("Show diff stat instead of patch"), {}, false}}},
        [service](const Json& args) -> std::string {
            auto path = args.value("path", "");
            bool staged = args.value("staged", false);
            bool stat = args.value("stat", false);
            auto result = command_detail::app_result_json(service->diff(path, staged, stat), [](const git::GitDiffResult& value) {
                return git::to_json(value);
            }).dump();
            return std::string(result.c_str(), result.size());
        },
        true);

    registry.register_tool(
        std::string("git_log"),
        std::string("Return structured git commit history. Read-only."),
        {{std::string("limit"), {std::string("integer"), std::string("Maximum commits to return, clamped to 200"), {}, false}},
         {std::string("path"), {std::string("string"), std::string("Optional path to filter commit history"), {}, false}}},
        [service](const Json& args) -> std::string {
            int limit = args.value("limit", 20);
            auto path = args.value("path", "");
            auto result = command_detail::app_result_json(service->log(limit, path), [](const git::GitLogResult& value) {
                return git::to_json(value);
            }).dump();
            return std::string(result.c_str(), result.size());
        },
        true);

    registry.register_tool(
        std::string("git_branch"),
        std::string("List, create, switch, or delete git branches. Mutating actions are permission-gated."),
        {{std::string("action"), {std::string("string"), std::string("Action: list, create, switch, delete"), {}, false}},
         {std::string("name"), {std::string("string"), std::string("Branch name for create/switch/delete"), {}, false}},
         {std::string("start_point"), {std::string("string"), std::string("Optional start point for create"), {}, false}},
         {std::string("force"), {std::string("boolean"), std::string("Force create/switch/delete when supported"), {}, false}}},
        [service, command_pipeline, request, project_path](const Json& args) -> std::string {
            auto action = args.value("action", "list");
            auto name = args.value("name", "");
            auto start_point = args.value("start_point", "");
            bool force = args.value("force", false);
            if (action == "list") {
                auto result = command_detail::app_result_json(service->list_branches(), [](const git::GitBranchListResult& value) {
                    return git::to_json(value);
                }).dump();
                return std::string(result.c_str(), result.size());
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
                return domain::AppResult<Json>::failure(domain::AppError::invalid_argument(std::string("invalid_arguments"), std::string("unsupported branch action")));
            }));
        });

    registry.register_tool(
        std::string("git_commit"),
        std::string("Create a git commit. Mutating and permission-gated."),
        {{std::string("message"), {std::string("string"), std::string("Commit message"), {}, true}},
         {std::string("paths"), {std::string("array"), std::string("Optional paths to stage before committing"), {}, false}},
         {std::string("all"), {std::string("boolean"), std::string("Stage tracked modifications with --all"), {}, false}},
         {std::string("amend"), {std::string("boolean"), std::string("Amend the previous commit"), {}, false}}},
        [service, command_pipeline, request, project_path](const Json& args) -> std::string {
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
        std::string("git_restore"),
        std::string("Restore tracked workspace files with git restore. Mutating and permission-gated."),
        {{std::string("paths"), {std::string("array"), std::string("Paths to restore; must be non-empty"), {}, true}},
         {std::string("staged"), {std::string("boolean"), std::string("Restore staged changes"), {}, false}},
         {std::string("worktree"), {std::string("boolean"), std::string("Restore worktree changes"), {}, false}}},
        [service, command_pipeline, request, project_path](const Json& args) -> std::string {
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
        std::string("git_worktree"),
        std::string("List, add, remove, or prune git worktrees. Mutating actions are permission-gated."),
        {{std::string("action"), {std::string("string"), std::string("Action: list, add, remove, prune"), {}, false}},
         {std::string("location"), {std::string("string"), std::string("Relative worktree location for add/remove"), {}, false}},
         {std::string("branch"), {std::string("string"), std::string("Branch name/ref for add"), {}, false}},
         {std::string("create_branch"), {std::string("boolean"), std::string("Create branch with -b during add"), {}, false}},
         {std::string("force"), {std::string("boolean"), std::string("Force add/remove when supported"), {}, false}}},
        [service, command_pipeline, request, project_path](const Json& args) -> std::string {
            auto action = args.value("action", "list");
            auto location = args.value("location", "");
            auto branch = args.value("branch", "");
            bool create_branch = args.value("create_branch", false);
            bool force = args.value("force", false);
            if (action == "list") {
                auto result = command_detail::app_result_json(service->list_worktrees(), [](const git::GitWorktreeListResult& value) {
                    return git::to_json(value);
                }).dump();
                return std::string(result.c_str(), result.size());
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
                return domain::AppResult<Json>::failure(domain::AppError::invalid_argument(std::string("invalid_arguments"), std::string("unsupported worktree action")));
            }));
        });
}

} // namespace ben_gear::tools
