#include "ben_gear/server/composition/command_api_composition.hpp"

#include "ben_gear/server/api/result_presenter.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace ben_gear::server::composition {

namespace {

namespace container = base::container;

workspace::WorkspaceContext workspace_context(CommandApiCompositionContext context,
                                               const container::String& workspace,
                                               const container::String& session_id,
                                               const container::String& username) {
    application::RequestContext request;
    request.username = username;
    request.workspace_name = workspace;
    request.session_id = session_id;
    auto resolved = context.workspace_resolver.resolve(request);
    return resolved.ok() ? resolved.value().to_workspace_context()
                         : workspace::WorkspaceContext{};
}

git::GitService git_service(CommandApiCompositionContext context,
                            const container::String& workspace,
                            const container::String& username) {
    return git::GitService(workspace_context(context, workspace, container::String(), username));
}

checkpoint::CheckpointService checkpoint_service(CommandApiCompositionContext context,
                                                  const container::String& workspace,
                                                  const container::String& session_id,
                                                  const container::String& username) {
    return checkpoint::CheckpointService(workspace_context(context, workspace, session_id, username));
}

test_loop::TestLoopService test_loop_service(CommandApiCompositionContext context,
                                             const container::String& workspace,
                                             const container::String& username) {
    return test_loop::TestLoopService(workspace_context(context, workspace, container::String(), username));
}

patch::PatchService patch_service(CommandApiCompositionContext context,
                                  const container::String& workspace,
                                  const container::String& session_id,
                                  const container::String& username) {
    return patch::PatchService(workspace_context(context, workspace, session_id, username));
}

Json append_audit_event(CommandApiCompositionContext context,
                        const container::String& workspace,
                        const container::String& session_id,
                        const container::String& username,
                        std::string_view category,
                        std::string_view action,
                        Json event) {
    auto ws = context.workspace_resolver.workspace_or_default(workspace);
    event["workspace"] = std::string(ws.data(), ws.size());
    event["session_id"] = std::string(session_id.data(), session_id.size());
    event["username"] = std::string(username.data(), username.size());
    event["category"] = std::string(category);
    event["action"] = std::string(action);
    audit::AuditStore store(context.workspace_resolver.user_dir_for(username) / "audit" / "events.jsonl");
    return store.append(std::move(event));
}


Json append_runtime_execution(CommandApiCompositionContext context,
                              const container::String& workspace,
                              const container::String& session_id,
                              const container::String& username,
                              Json execution) {
    auto ws = context.workspace_resolver.workspace_or_default(workspace);
    execution["workspace"] = std::string(ws.data(), ws.size());
    execution["session_id"] = std::string(session_id.data(), session_id.size());
    execution["username"] = std::string(username.data(), username.size());
    audit::RuntimeExecutionStore store(context.workspace_resolver.user_dir_for(username) / "runtime" / "executions.jsonl");
    return store.append(std::move(execution));
}

Json permission_session_not_found() {
    return Json{{"success", false}, {"error_type", "session_not_found"}, {"message", "session not found"}};
}

Json check_tool_permission(CommandApiCompositionContext context,
                           const container::String& workspace,
                           const container::String& session_id,
                           const container::String& username,
                           std::string_view tool_name,
                           const Json& arguments) {
    auto ws = context.workspace_resolver.workspace_or_default(workspace);
    auto entry = context.session_pool.get(session_id, username, ws);
    if (!entry || !entry->agent || !entry->agent->resources() || !entry->agent->resources()->policy_engine()) return permission_session_not_found();
    auto decision = entry->agent->resources()->policy_engine()->evaluate_tool_permission(tool_name, arguments);
    if (decision.allowed()) {
        Json result{{"success", true}, {"policy_effect", "allow"}, {"policy_key", decision.policy_key}};
        append_audit_event(context, workspace, session_id, username, "permission", "allowed",
                           Json{{"tool_name", std::string(tool_name)},
                                {"policy_key", decision.policy_key},
                                {"outcome", "success"},
                                {"arguments", arguments}});
        return result;
    }
    auto result = permission::to_json(decision);
    append_audit_event(context, workspace, session_id, username, "permission", result.value("error_type", "") == "permission_required" ? "requested" : "denied",
                       Json{{"tool_name", std::string(tool_name)},
                            {"policy_key", result.value("policy_key", "")},
                            {"permission_id", result.value("permission_id", "")},
                            {"outcome", result.value("error_type", "") == "permission_required" ? "pending" : "denied"},
                            {"arguments", arguments},
                            {"resource", result.contains("resource") ? result["resource"] : Json::object()}});
    return result;
}

application::CommandPipeline build_command_pipeline(CommandApiCompositionContext context) {
    return application::make_command_pipeline(application::CommandGovernanceConfig{
        [context](const container::String& workspace,
                  const container::String& session_id,
                  const container::String& username,
                  std::string_view tool_name,
                  const Json& arguments) {
            return check_tool_permission(context, workspace, session_id, username, tool_name, arguments);
        },
        [context](const application::CommandDescriptor& command) {
            if (!command.mutates_workspace || command.affected_paths.empty()) return domain::AppResult<void>::success();
            std::vector<std::string> paths;
            for (const auto& path : command.affected_paths) paths.emplace_back(path.c_str());
            auto checkpoint = checkpoint_service(context, command.workspace_name, command.session_id, command.username);
            auto result = checkpoint.create(paths, "auto checkpoint before " + std::string(command.action.c_str()));
            if (result.ok()) return domain::AppResult<void>::success();
            return domain::AppResult<void>::failure(result.error());
        },
        [context](const container::String& workspace,
                  const container::String& session_id,
                  const container::String& username,
                  const container::String& category,
                  const container::String& action,
                  const Json& details) {
            return append_audit_event(context, workspace, session_id, username, std::string(category.c_str()), std::string(action.c_str()), details);
        },
        [context](const container::String& workspace,
                  const container::String& session_id,
                  const container::String& username,
                  const Json& execution) {
            return append_runtime_execution(context, workspace, session_id, username, execution);
        }});
}

application::CommandDescriptor build_command(CommandApiCompositionContext context,
                                             const container::String& workspace,
                                             const container::String& session_id,
                                             const container::String& username,
                                             std::string_view action) {
    auto ws = context.workspace_resolver.workspace_or_default(workspace);
    application::CommandDescriptor command;
    command.action = container::String(action.data(), action.size());
    command.username = username;
    command.workspace_name = ws;
    command.session_id = session_id;
    command.project_path = context.workspace_resolver.project_path_for(username, ws);
    return command;
}

} // namespace


application::CommandPipeline make_server_command_pipeline(CommandApiCompositionContext context) {
    return build_command_pipeline(context);
}

PermissionApiService make_permission_api_service(CommandApiCompositionContext context) {
    PermissionApiService svc;
    svc.list_pending = [context](const container::String& workspace,
                                 const container::String& session_id,
                                 const container::String& username) {
        auto ws = context.workspace_resolver.workspace_or_default(workspace);
        auto entry = context.session_pool.get(session_id, username, ws);
        if (!entry || !entry->agent || !entry->agent->resources() || !entry->agent->resources()->policy_engine()) return permission_session_not_found();
        return entry->agent->resources()->policy_engine()->list_pending();
    };
    svc.approve = [context](const container::String& workspace,
                            const container::String& session_id,
                            const container::String& username,
                            std::string_view permission_id,
                            bool allow_session) {
        auto ws = context.workspace_resolver.workspace_or_default(workspace);
        auto entry = context.session_pool.get(session_id, username, ws);
        if (!entry || !entry->agent || !entry->agent->resources() || !entry->agent->resources()->policy_engine()) return permission_session_not_found();
        auto result = entry->agent->resources()->policy_engine()->approve(permission_id, allow_session);
        append_audit_event(context, workspace, session_id, username, "permission", "approved",
                           Json{{"permission_id", std::string(permission_id)},
                                {"allow_session", allow_session},
                                {"outcome", result.value("success", false) ? "success" : "failed"},
                                {"result", result}});
        return result;
    };
    svc.deny = [context](const container::String& workspace,
                         const container::String& session_id,
                         const container::String& username,
                         std::string_view permission_id) {
        auto ws = context.workspace_resolver.workspace_or_default(workspace);
        auto entry = context.session_pool.get(session_id, username, ws);
        if (!entry || !entry->agent || !entry->agent->resources() || !entry->agent->resources()->policy_engine()) return permission_session_not_found();
        auto result = entry->agent->resources()->policy_engine()->deny_pending(permission_id);
        append_audit_event(context, workspace, session_id, username, "permission", "denied_by_user",
                           Json{{"permission_id", std::string(permission_id)},
                                {"outcome", result.value("success", false) ? "success" : "failed"},
                                {"result", result}});
        return result;
    };
    return svc;
}

GitApiService make_git_api_service(CommandApiCompositionContext context) {
    GitApiService svc;
    auto pipeline = build_command_pipeline(context);
    svc.status = [context](const container::String& workspace,
                           const container::String& username) {
        return git::to_json(git_service(context, workspace, username).status());
    };
    svc.diff = [context](const container::String& workspace,
                         const container::String& username,
                         std::string_view path,
                         bool staged,
                         bool stat,
                         bool preview) {
        auto result = git_service(context, workspace, username).diff(std::string(path), staged, stat);
        if (!result.ok()) return app_error_json(result.error());
        auto json = git::to_json(result.value());
        json["path"] = std::string(path);
        json["empty"] = result.value().diff.empty();
        if (preview && !stat) {
            auto parsed = result.value().diff.empty() ? patch::empty_patch_preview() : patch::parse_unified_diff(result.value().diff);
            parsed.can_apply = false;
            json["preview"] = patch::to_json(parsed);
        }
        return json;
    };
    svc.log = [context](const container::String& workspace,
                        const container::String& username,
                        std::string_view path,
                        int limit) {
        auto result = git_service(context, workspace, username).log(limit, std::string(path));
        if (!result.ok()) return app_error_json(result.error());
        auto json = git::to_json(result.value());
        json["path"] = std::string(path);
        return json;
    };
    svc.branches = [context](const container::String& workspace,
                             const container::String& username) {
        return app_result_json(git_service(context, workspace, username).list_branches(), [](const git::GitBranchListResult& result) {
            return git::to_json(result);
        });
    };
    svc.worktrees = [context](const container::String& workspace,
                              const container::String& username) {
        return app_result_json(git_service(context, workspace, username).list_worktrees(), [](const git::GitWorktreeListResult& result) {
            return git::to_json(result);
        });
    };
    svc.create_branch = [context, pipeline](const container::String& workspace,
                                            const container::String& session_id,
                                            const container::String& username,
                                            std::string_view name,
                                            std::string_view start_point,
                                            bool force) {
        auto command = build_command(context, workspace, session_id, username, "git.branch.create");
        command.subject = container::String(name.data(), name.size());
        command.risk = force ? application::CommandRisk::destructive : application::CommandRisk::workspace_write;
        command.runs_command = true;
        command.force = force;
        return app_error_json_or_value(pipeline.execute<Json>(command, [&]() {
            return presented_command_result(git_service(context, workspace, username).create_branch(std::string(name), std::string(start_point), force), [](const git::GitBranchMutationResult& result) {
                return git::to_json(result);
            });
        }));
    };
    svc.switch_branch = [context, pipeline](const container::String& workspace,
                                            const container::String& session_id,
                                            const container::String& username,
                                            std::string_view name,
                                            bool force) {
        auto command = build_command(context, workspace, session_id, username, "git.branch.switch");
        command.subject = container::String(name.data(), name.size());
        command.risk = force ? application::CommandRisk::destructive : application::CommandRisk::workspace_write;
        command.runs_command = true;
        command.force = force;
        return app_error_json_or_value(pipeline.execute<Json>(command, [&]() {
            return presented_command_result(git_service(context, workspace, username).switch_branch(std::string(name), force), [](const git::GitBranchMutationResult& result) {
                return git::to_json(result);
            });
        }));
    };
    svc.delete_branch = [context, pipeline](const container::String& workspace,
                                            const container::String& session_id,
                                            const container::String& username,
                                            std::string_view name,
                                            bool force) {
        auto command = build_command(context, workspace, session_id, username, "git.branch.delete");
        command.subject = container::String(name.data(), name.size());
        command.risk = application::CommandRisk::destructive;
        command.runs_command = true;
        command.force = force;
        return app_error_json_or_value(pipeline.execute<Json>(command, [&]() {
            return presented_command_result(git_service(context, workspace, username).delete_branch(std::string(name), force), [](const git::GitBranchMutationResult& result) {
                return git::to_json(result);
            });
        }));
    };
    svc.restore = [context, pipeline](const container::String& workspace,
                                      const container::String& session_id,
                                      const container::String& username,
                                      const std::vector<std::string>& paths,
                                      bool staged,
                                      bool worktree) {
        auto command = build_command(context, workspace, session_id, username, "git.restore");
        command.risk = application::CommandRisk::workspace_write;
        command.mutates_workspace = worktree;
        command.runs_command = true;
        command.staged = staged;
        command.worktree = worktree;
        for (const auto& path : paths) command.affected_paths.push_back(container::String(path.c_str()));
        return app_error_json_or_value(pipeline.execute<Json>(command, [&]() {
            return presented_command_result(git_service(context, workspace, username).restore(paths, staged, worktree), [](const git::GitRestoreResult& result) {
                return git::to_json(result);
            });
        }));
    };
    svc.commit = [context, pipeline](const container::String& workspace,
                                     const container::String& session_id,
                                     const container::String& username,
                                     std::string_view message,
                                     const std::vector<std::string>& paths,
                                     bool all,
                                     bool amend) {
        auto command = build_command(context, workspace, session_id, username, "git.commit");
        command.subject = container::String(message.data(), message.size());
        command.risk = amend ? application::CommandRisk::destructive : application::CommandRisk::workspace_write;
        command.runs_command = true;
        command.all = all;
        command.amend = amend;
        for (const auto& path : paths) command.affected_paths.push_back(container::String(path.c_str()));
        return app_error_json_or_value(pipeline.execute<Json>(command, [&]() {
            return presented_command_result(git_service(context, workspace, username).commit(std::string(message), paths, all, amend), [](const git::GitCommitResult& result) {
                return git::to_json(result);
            });
        }));
    };
    return svc;
}

PatchApiService make_patch_api_service(CommandApiCompositionContext context) {
    PatchApiService svc;
    application::PatchUseCases patch_use_cases(context.workspace_resolver, build_command_pipeline(context));
    svc.preview_patch = [patch_use_cases](const container::String& workspace,
                                          const container::String& session_id,
                                          const container::String& username,
                                          std::string_view unified_diff) mutable {
        application::PatchPreviewQuery query;
        query.request.username = username;
        query.request.workspace_name = workspace;
        query.request.session_id = session_id;
        query.unified_diff = std::string(unified_diff);
        auto result = patch_use_cases.preview_patch(query);
        if (!result.ok()) return app_error_json(result.error());
        return patch::to_json(result.value());
    };
    svc.apply_patch = [patch_use_cases](const container::String& workspace,
                                        const container::String& session_id,
                                        const container::String& username,
                                        std::string_view unified_diff,
                                        std::string_view description) mutable {
        application::PatchApplyCommand command;
        command.request.username = username;
        command.request.workspace_name = workspace;
        command.request.session_id = session_id;
        command.unified_diff = std::string(unified_diff);
        command.description = std::string(description);
        auto result = patch_use_cases.apply_patch(command);
        if (!result.ok()) return app_error_json(result.error());
        return patch::to_json(result.value());
    };
    svc.safe_code_change = [safe_service = application::SafeCodeChangeService(context.workspace_resolver, build_command_pipeline(context))](
                               const container::String& workspace,
                               const container::String& session_id,
                               const container::String& username,
                               std::string_view unified_diff,
                               std::string_view description,
                               std::string_view test_command,
                               std::string_view test_cwd,
                               int test_timeout_seconds,
                               int test_max_output_bytes) mutable {
        application::SafeCodeChangeCommand command;
        command.request.username = username;
        command.request.workspace_name = workspace;
        command.request.session_id = session_id;
        command.unified_diff = std::string(unified_diff);
        command.description = std::string(description);
        command.test_command = std::string(test_command);
        command.test_cwd = std::string(test_cwd);
        command.test_timeout_seconds = test_timeout_seconds;
        command.test_max_output_bytes = test_max_output_bytes;
        auto result = safe_service.run(command);
        if (!result.ok()) return app_error_json(result.error());
        return application::to_json(result.value());
    };
    svc.list_changes = [context](const container::String& workspace,
                                 const container::String& session_id,
                                 const container::String& username) {
        return app_result_json(patch_service(context, workspace, session_id, username).list_changes(), [](const patch::PatchListChangesResult& result) {
            return patch::to_json(result);
        });
    };
    svc.read_change = [context](const container::String& workspace,
                                const container::String& session_id,
                                const container::String& username,
                                std::string_view change_id) {
        auto result = patch_service(context, workspace, session_id, username).read_change(change_id);
        if (!result.ok()) return app_error_json(result.error());
        Json json = patch::to_json(result.value());
        if (json.contains("change") && json["change"].is_object()) {
            Json change = json["change"];
            if (change.contains("files") && change["files"].is_array()) {
                Json safe_files = Json::array();
                for (auto file : change["files"]) {
                    if (file.is_object()) file.erase("before_content");
                    safe_files.push_back(std::move(file));
                }
                change["files"] = std::move(safe_files);
                json["change"] = std::move(change);
            }
        }
        return json;
    };
    svc.revert_change = [patch_use_cases](const container::String& workspace,
                                          const container::String& session_id,
                                          const container::String& username,
                                          std::string_view change_id,
                                          bool force) mutable {
        application::PatchRevertCommand command;
        command.request.username = username;
        command.request.workspace_name = workspace;
        command.request.session_id = session_id;
        command.change_id = std::string(change_id);
        command.force = force;
        auto result = patch_use_cases.revert_patch(command);
        if (!result.ok()) return app_error_json(result.error());
        return patch::to_json(result.value());
    };
    return svc;
}

CheckpointApiService make_checkpoint_api_service(CommandApiCompositionContext context) {
    CheckpointApiService svc;
    auto pipeline = build_command_pipeline(context);
    svc.list = [context](const container::String& workspace,
                         const container::String& session_id,
                         const container::String& username) {
        return app_result_json(checkpoint_service(context, workspace, session_id, username).list(), [](const checkpoint::CheckpointListResult& result) {
            return checkpoint::to_json(result);
        });
    };
    svc.read = [context](const container::String& workspace,
                         const container::String& session_id,
                         const container::String& username,
                         std::string_view checkpoint_id) {
        return app_result_json(checkpoint_service(context, workspace, session_id, username).read(checkpoint_id), [](const checkpoint::CheckpointReadResult& result) {
            return checkpoint::to_json(result);
        });
    };
    svc.restore = [context, pipeline](const container::String& workspace,
                                      const container::String& session_id,
                                      const container::String& username,
                                      std::string_view checkpoint_id,
                                      const std::vector<std::string>& paths,
                                      bool force) {
        auto command = build_command(context, workspace, session_id, username, "checkpoint.restore");
        command.subject = container::String(checkpoint_id.data(), checkpoint_id.size());
        command.risk = force ? application::CommandRisk::destructive : application::CommandRisk::workspace_write;
        command.mutates_workspace = true;
        command.force = force;
        for (const auto& path : paths) command.affected_paths.push_back(container::String(path.c_str()));
        if (command.affected_paths.empty()) {
            auto checkpoint = checkpoint_service(context, workspace, session_id, username).read(checkpoint_id);
            if (checkpoint.ok()) {
                for (const auto& file : checkpoint.value().checkpoint.files) {
                    if (!file.path.empty()) command.affected_paths.push_back(container::String(file.path.c_str()));
                }
            }
        }
        return app_error_json_or_value(pipeline.execute<Json>(command, [&]() {
            return presented_command_result(checkpoint_service(context, workspace, session_id, username).restore(checkpoint_id, paths, force), [](const checkpoint::CheckpointRestoreResult& result) {
                return checkpoint::to_json(result);
            });
        }));
    };
    svc.remove = [context, pipeline](const container::String& workspace,
                                     const container::String& session_id,
                                     const container::String& username,
                                     std::string_view checkpoint_id) {
        auto command = build_command(context, workspace, session_id, username, "checkpoint.delete");
        command.subject = container::String(checkpoint_id.data(), checkpoint_id.size());
        command.risk = application::CommandRisk::destructive;
        return app_error_json_or_value(pipeline.execute<Json>(command, [&]() {
            return presented_command_result(checkpoint_service(context, workspace, session_id, username).remove(checkpoint_id), [](const checkpoint::CheckpointRemoveResult& result) {
                return checkpoint::to_json(result);
            });
        }));
    };
    return svc;
}

TestLoopApiService make_test_loop_api_service(CommandApiCompositionContext context) {
    TestLoopApiService svc;
    auto pipeline = build_command_pipeline(context);
    svc.inspect = [context](const container::String& workspace,
                            const container::String& username) {
        return app_result_json(test_loop_service(context, workspace, username).inspect(), [](const test_loop::TestLoopInspectResult& result) {
            return test_loop::to_json(result);
        });
    };
    svc.run = [context, pipeline](const container::String& workspace,
                                  const container::String& session_id,
                                  const container::String& username,
                                  std::string_view command_text,
                                  std::string_view cwd,
                                  int timeout_seconds,
                                  int max_output_bytes) {
        auto command = build_command(context, workspace, session_id, username, "test.run");
        command.subject = container::String(command_text.data(), command_text.size());
        command.working_directory = container::String(cwd.data(), cwd.size());
        command.risk = application::CommandRisk::command_execution;
        command.runs_command = true;
        command.timeout_seconds = timeout_seconds;
        command.max_output_bytes = max_output_bytes;
        return app_error_json_or_value(pipeline.execute<Json>(command, [&]() {
            auto result = test_loop_service(context, workspace, username).run(std::string(command_text), std::string(cwd), timeout_seconds, max_output_bytes);
            if (!result.ok()) return domain::AppResult<Json>::failure(result.error());
            return domain::AppResult<Json>::success(test_loop::to_json(result.value()));
        }));
    };
    return svc;
}

} // namespace ben_gear::server::composition
