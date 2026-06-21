#include "ben_gear/application/command_governance.hpp"
#include "ben_gear/test/test_framework.hpp"

#include <string>
#include <vector>

namespace {

namespace application = ben_gear::application;
using ben_gear::Json;
using ben_gear::base::container::String;
using ben_gear::domain::AppResult;

application::CommandDescriptor base_command(std::string_view action) {
    application::CommandDescriptor command;
    command.action = String(action.data(), action.size());
    command.username = String("alice");
    command.workspace_name = String("default");
    command.session_id = String("sid-1");
    command.project_path = String("/repo");
    return command;
}

} // namespace

TEST(CommandGovernanceTest, MapsCommandActionsToPermissionTools) {
    EXPECT_EQ(application::command_tool_name(base_command("patch.apply")), "apply_patch");
    EXPECT_EQ(application::command_tool_name(base_command("patch.revert")), "revert_patch");
    EXPECT_EQ(application::command_tool_name(base_command("git.branch.create")), "git_branch");
    EXPECT_EQ(application::command_tool_name(base_command("git.restore")), "git_restore");
    EXPECT_EQ(application::command_tool_name(base_command("git.commit")), "git_commit");
    EXPECT_EQ(application::command_tool_name(base_command("git.worktree.add")), "git_worktree");
    EXPECT_EQ(application::command_tool_name(base_command("checkpoint.restore")), "restore_checkpoint");
    EXPECT_EQ(application::command_tool_name(base_command("checkpoint.delete")), "delete_checkpoint");
    EXPECT_EQ(application::command_tool_name(base_command("test.run")), "run_tests");
    EXPECT_EQ(application::command_tool_name(base_command("unknown")), "");
}

TEST(CommandGovernanceTest, BuildsGitCommitPermissionArguments) {
    auto command = base_command("git.commit");
    command.subject = String("save work");
    command.all = true;
    command.amend = true;
    command.affected_paths.push_back(String("src/a.cpp"));
    command.affected_paths.push_back(String("include/a.hpp"));

    auto args = application::command_permission_arguments(command);

    EXPECT_EQ(args.value("message", ""), "save work");
    EXPECT_TRUE(args.value("all", false));
    EXPECT_TRUE(args.value("amend", false));
    EXPECT_EQ(args.value("project_path", ""), "/repo");
    ASSERT_TRUE(args["paths"].is_array());
    EXPECT_EQ(args["paths"].size(), static_cast<size_t>(2));
    EXPECT_EQ(args["paths"][0].get<std::string>(), "src/a.cpp");
    EXPECT_EQ(args["paths"][1].get<std::string>(), "include/a.hpp");
}

TEST(CommandGovernanceTest, BuildsGitWorktreePermissionArguments) {
    auto command = base_command("git.worktree.add");
    command.subject = String("../linked-worktree");
    command.affected_paths.push_back(String("../linked-worktree"));
    command.all = true;
    command.force = true;

    auto args = application::command_permission_arguments(command);

    EXPECT_EQ(args.value("action", ""), "add");
    EXPECT_EQ(args.value("location", ""), "../linked-worktree");
    EXPECT_TRUE(args.value("create_branch", false));
    EXPECT_TRUE(args.value("force", false));
    EXPECT_EQ(args.value("project_path", ""), "/repo");
    ASSERT_TRUE(args["paths"].is_array());
    EXPECT_EQ(args["paths"][0].get<std::string>(), "../linked-worktree");
}

TEST(CommandGovernanceTest, BuildsTestRunPermissionArguments) {
    auto command = base_command("test.run");
    command.subject = String("ctest --output-on-failure");
    command.working_directory = String("build");
    command.timeout_seconds = 45;
    command.max_output_bytes = 12000;

    auto args = application::command_permission_arguments(command);

    EXPECT_EQ(args.value("command", ""), "ctest --output-on-failure");
    EXPECT_EQ(args.value("cwd", ""), "build");
    EXPECT_EQ(args.value("timeout_seconds", 0), 45);
    EXPECT_EQ(args.value("max_output_bytes", 0), 12000);
    EXPECT_EQ(args.value("project_path", ""), "/repo");
}

TEST(CommandGovernanceTest, PipelineAuthorizesCheckpointsExecutesAndAudits) {
    std::vector<std::string> calls;
    std::string permission_tool;
    Json permission_arguments;
    Json audit_details;

    auto pipeline = application::make_command_pipeline(application::CommandGovernanceConfig{
        [&](const String& workspace, const String& session_id, const String& username, std::string_view tool_name, const Json& arguments) {
            calls.push_back("authorize");
            EXPECT_EQ(workspace, String("default"));
            EXPECT_EQ(session_id, String("sid-1"));
            EXPECT_EQ(username, String("alice"));
            permission_tool = std::string(tool_name);
            permission_arguments = arguments;
            return Json{{"success", true}, {"policy_effect", "allow"}};
        },
        [&](const application::CommandDescriptor& command) {
            calls.push_back("checkpoint");
            EXPECT_TRUE(command.mutates_workspace);
            return AppResult<void>::success();
        },
        [&](const String&, const String&, const String&, const String&, const String& action, const Json& details) {
            calls.push_back("audit");
            EXPECT_EQ(action, String("git.restore"));
            audit_details = details;
        }});

    auto command = base_command("git.restore");
    command.risk = application::CommandRisk::workspace_write;
    command.mutates_workspace = true;
    command.staged = true;
    command.worktree = true;
    command.affected_paths.push_back(String("src/a.cpp"));

    auto result = pipeline.execute<Json>(command, [&]() {
        calls.push_back("execute");
        return AppResult<Json>::success(Json{{"success", true}});
    });

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(calls, (std::vector<std::string>{"authorize", "checkpoint", "execute", "audit"}));
    EXPECT_EQ(permission_tool, "git_restore");
    EXPECT_TRUE(permission_arguments.value("staged", false));
    EXPECT_EQ(permission_arguments["paths"][0].get<std::string>(), "src/a.cpp");
    EXPECT_EQ(audit_details.value("risk", ""), "workspace_write");
    EXPECT_EQ(audit_details.value("outcome", ""), "success");
}

TEST(CommandGovernanceTest, PipelineStopsBeforeCheckpointWhenPermissionDenied) {
    std::vector<std::string> calls;

    auto pipeline = application::make_command_pipeline(application::CommandGovernanceConfig{
        [&](const String&, const String&, const String&, std::string_view, const Json&) {
            calls.push_back("authorize");
            return Json{{"success", false}, {"error_type", "permission_required"}, {"permission_id", "perm-1"}};
        },
        [&](const application::CommandDescriptor&) {
            calls.push_back("checkpoint");
            return AppResult<void>::success();
        },
        [&](const String&, const String&, const String&, const String&, const String&, const Json&) {
            calls.push_back("audit");
        }});

    auto command = base_command("test.run");
    auto result = pipeline.execute<Json>(command, [&]() {
        calls.push_back("execute");
        return AppResult<Json>::success(Json{{"success", true}});
    });

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(std::string(result.error().code.c_str()), "permission_required");
    EXPECT_THAT(std::string(result.error().details_json.c_str()), testing::HasSubstr("perm-1"));
    EXPECT_EQ(calls, (std::vector<std::string>{"authorize", "audit"}));
}

TEST(CommandGovernanceTest, PipelineRejectsUnknownCommandBeforeExecution) {
    std::vector<std::string> calls;

    auto pipeline = application::make_command_pipeline(application::CommandGovernanceConfig{
        [&](const String&, const String&, const String&, std::string_view, const Json&) {
            calls.push_back("authorize");
            return Json{{"success", true}};
        },
        [&](const application::CommandDescriptor&) {
            calls.push_back("checkpoint");
            return AppResult<void>::success();
        },
        [&](const String&, const String&, const String&, const String&, const String&, const Json&) {
            calls.push_back("audit");
        }});

    auto result = pipeline.execute<Json>(base_command("unknown.action"), [&]() {
        calls.push_back("execute");
        return AppResult<Json>::success(Json{{"success", true}});
    });

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(std::string(result.error().code.c_str()), "unknown_command");
    EXPECT_EQ(calls, (std::vector<std::string>{"audit"}));
}
