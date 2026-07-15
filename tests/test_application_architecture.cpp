#include "application/command_pipeline.hpp"
#include "application/patch_use_cases.hpp"
#include "application/safe_code_change_service.hpp"
#include "application/workspace_resolver.hpp"
#include "test_framework.hpp"
#include "workspace/manager.hpp"

#include "test_util.hpp"

#include <cstdlib>
#include <fstream>

using bengear::test::TmpDirTest;

namespace {

using ben_gear::application::CommandDescriptor;
using ben_gear::application::CommandPipeline;
using ben_gear::application::CommandPipelineHooks;
using ben_gear::application::PatchPreviewQuery;
using ben_gear::application::PatchUseCases;
using ben_gear::application::RequestContext;
using ben_gear::application::WorkspaceResolver;
using ben_gear::application::WorkspaceResolverConfig;
using ben_gear::domain::AppError;
using ben_gear::domain::AppResult;

class ApplicationArchitectureTest : public TmpDirTest {};

} // namespace

TEST_F(ApplicationArchitectureTest, WorkspaceResolverBuildsResolvedWorkspaceContext) {
    WorkspaceResolver resolver(WorkspaceResolverConfig{dir(), std::string("default"), std::string("/fallback/project")});

    RequestContext request;
    request.username = std::string("alice");
    request.workspace_name = std::string("project1");
    request.session_id = std::string("session-1");

    auto result = resolver.resolve(request);

    ASSERT_TRUE(result.ok());
    const auto& resolved = result.value();
    EXPECT_EQ(resolved.request.username, "alice");
    EXPECT_EQ(resolved.request.workspace_name, "project1");
    EXPECT_EQ(resolved.user_dir, dir() / "users" / "alice");
    EXPECT_EQ(resolved.workspace_dir, dir() / "users" / "alice" / "workspaces" / "project1");
    EXPECT_EQ(resolved.project_path, "/fallback/project");

    auto ws_ctx = resolved.to_workspace_context();
    EXPECT_EQ(ws_ctx.username, "alice");
    EXPECT_EQ(ws_ctx.workspace_name, "project1");
    EXPECT_EQ(ws_ctx.session_id, "session-1");
}

TEST_F(ApplicationArchitectureTest, WorkspaceResolverUsesDefaultWorkspaceAndStoredProjectPath) {
    WorkspaceResolver resolver(WorkspaceResolverConfig{dir(), std::string("default"), std::string("/fallback/project")});
    ben_gear::workspace::WorkspaceManager manager(dir() / "users" / "bob");
    auto created = manager.create(std::string("code"), std::string("/repo/code"));
    ASSERT_TRUE(created.has_value());

    RequestContext request;
    request.username = std::string("bob");
    request.workspace_name = std::string("code");

    auto result = resolver.resolve(request);

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().request.workspace_name, "code");
    EXPECT_EQ(result.value().project_path, "/repo/code");

    request.workspace_name = std::string();
    auto default_result = resolver.resolve(request);
    ASSERT_TRUE(default_result.ok());
    EXPECT_EQ(default_result.value().request.workspace_name, "default");
}

TEST_F(ApplicationArchitectureTest, PatchUseCaseReturnsTypedPreview) {
    auto project_dir = dir() / "project";
    std::filesystem::create_directories(project_dir);
    {
        std::ofstream file(project_dir / "hello.txt", std::ios::binary | std::ios::trunc);
        file << "old\n";
    }

    WorkspaceResolver resolver(WorkspaceResolverConfig{dir(), std::string("default"), project_dir.string()});
    PatchUseCases patches(resolver);

    PatchPreviewQuery query;
    query.request.username = std::string("alice");
    query.request.workspace_name = std::string("default");
    query.request.session_id = std::string("sid-1");
    query.unified_diff = "--- a/hello.txt\n+++ b/hello.txt\n@@ -1 +1 @@\n-old\n+new\n";

    auto result = patches.preview_patch(query);

    ASSERT_TRUE(result.ok());
    EXPECT_TRUE(result.value().success);
    EXPECT_EQ(result.value().files.size(), static_cast<size_t>(1));
    EXPECT_EQ(result.value().additions, 1);
    EXPECT_EQ(result.value().deletions, 1);
}

TEST_F(ApplicationArchitectureTest, PatchApplyUseCaseRunsCommandPipelineAndAuditsSuccess) {
    auto project_dir = dir() / "project";
    std::filesystem::create_directories(project_dir);
    {
        std::ofstream file(project_dir / "hello.txt", std::ios::binary | std::ios::trunc);
        file << "old\n";
    }

    WorkspaceResolver resolver(WorkspaceResolverConfig{dir(), std::string("default"), project_dir.string()});
    std::vector<std::string> calls;
    PatchUseCases patches(resolver, CommandPipeline(CommandPipelineHooks{
        [&](const CommandDescriptor& command) {
            EXPECT_EQ(command.action, "patch.apply");
            EXPECT_TRUE(command.mutates_workspace);
            calls.push_back("validate");
            return AppResult<void>::success();
        },
        [&](const CommandDescriptor& command) {
            EXPECT_EQ(command.risk, ben_gear::application::CommandRisk::workspace_write);
            calls.push_back("authorize");
            return AppResult<void>::success();
        },
        [&](const CommandDescriptor&) {
            calls.push_back("checkpoint");
            return AppResult<void>::success();
        },
        [&](const CommandDescriptor&, const AppError* error) {
            calls.push_back(error ? "audit:error" : "audit:success");
        }}));

    ben_gear::application::PatchApplyCommand command;
    command.request.username = std::string("alice");
    command.request.workspace_name = std::string("default");
    command.request.session_id = std::string("sid-1");
    command.unified_diff = "--- a/hello.txt\n+++ b/hello.txt\n@@ -1 +1 @@\n-old\n+new\n";
    command.description = "update hello";

    auto result = patches.apply_patch(command);

    ASSERT_TRUE(result.ok());
    EXPECT_FALSE(result.value().change_id.empty());
    EXPECT_EQ(result.value().files_changed, 1);
    EXPECT_EQ(result.value().additions, 1);
    EXPECT_EQ(result.value().deletions, 1);
    EXPECT_EQ(calls, (std::vector<std::string>{"validate", "authorize", "checkpoint", "audit:success"}));
    std::ifstream file(project_dir / "hello.txt", std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "new");
}

TEST_F(ApplicationArchitectureTest, PatchApplyUseCaseStopsBeforeWriteWhenAuthorizationFails) {
    auto project_dir = dir() / "project";
    std::filesystem::create_directories(project_dir);
    {
        std::ofstream file(project_dir / "hello.txt", std::ios::binary | std::ios::trunc);
        file << "old\n";
    }

    WorkspaceResolver resolver(WorkspaceResolverConfig{dir(), std::string("default"), project_dir.string()});
    std::vector<std::string> calls;
    PatchUseCases patches(resolver, CommandPipeline(CommandPipelineHooks{
        [&](const CommandDescriptor&) {
            calls.push_back("validate");
            return AppResult<void>::success();
        },
        [&](const CommandDescriptor&) {
            calls.push_back("authorize");
            return AppResult<void>::failure(AppError::permission_denied(std::string("denied"), std::string("denied")));
        },
        [&](const CommandDescriptor&) {
            calls.push_back("checkpoint");
            return AppResult<void>::success();
        },
        [&](const CommandDescriptor&, const AppError*) {
            calls.push_back("audit");
        }}));

    ben_gear::application::PatchApplyCommand command;
    command.request.username = std::string("alice");
    command.request.workspace_name = std::string("default");
    command.request.session_id = std::string("sid-1");
    command.unified_diff = "--- a/hello.txt\n+++ b/hello.txt\n@@ -1 +1 @@\n-old\n+new\n";

    auto result = patches.apply_patch(command);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, "denied");
    EXPECT_EQ(calls, (std::vector<std::string>{"validate", "authorize", "audit"}));
    std::ifstream file(project_dir / "hello.txt", std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "old\n");
}

TEST_F(ApplicationArchitectureTest, PatchApplyUseCaseStopsBeforeWriteWhenCheckpointFails) {
    auto project_dir = dir() / "project";
    std::filesystem::create_directories(project_dir);
    {
        std::ofstream file(project_dir / "hello.txt", std::ios::binary | std::ios::trunc);
        file << "old\n";
    }

    WorkspaceResolver resolver(WorkspaceResolverConfig{dir(), std::string("default"), project_dir.string()});
    std::vector<std::string> calls;
    PatchUseCases patches(resolver, CommandPipeline(CommandPipelineHooks{
        [&](const CommandDescriptor&) {
            calls.push_back("validate");
            return AppResult<void>::success();
        },
        [&](const CommandDescriptor&) {
            calls.push_back("authorize");
            return AppResult<void>::success();
        },
        [&](const CommandDescriptor&) {
            calls.push_back("checkpoint");
            return AppResult<void>::failure(AppError::unavailable(std::string("checkpoint_failed"), std::string("checkpoint failed")));
        },
        [&](const CommandDescriptor&, const AppError*) {
            calls.push_back("audit");
        }}));

    ben_gear::application::PatchApplyCommand command;
    command.request.username = std::string("alice");
    command.request.workspace_name = std::string("default");
    command.request.session_id = std::string("sid-1");
    command.unified_diff = "--- a/hello.txt\n+++ b/hello.txt\n@@ -1 +1 @@\n-old\n+new\n";

    auto result = patches.apply_patch(command);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, "checkpoint_failed");
    EXPECT_EQ(calls, (std::vector<std::string>{"validate", "authorize", "checkpoint", "audit"}));
    std::ifstream file(project_dir / "hello.txt", std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "old\n");
}

TEST_F(ApplicationArchitectureTest, PatchRevertUseCaseRunsCommandPipelineAndRestoresFile) {
    auto project_dir = dir() / "project";
    std::filesystem::create_directories(project_dir);
    {
        std::ofstream file(project_dir / "hello.txt", std::ios::binary | std::ios::trunc);
        file << "old\n";
    }

    WorkspaceResolver resolver(WorkspaceResolverConfig{dir(), std::string("default"), project_dir.string()});
    PatchUseCases apply_patches(resolver);
    ben_gear::application::PatchApplyCommand apply;
    apply.request.username = std::string("alice");
    apply.request.workspace_name = std::string("default");
    apply.request.session_id = std::string("sid-1");
    apply.unified_diff = "--- a/hello.txt\n+++ b/hello.txt\n@@ -1 +1 @@\n-old\n+new\n";
    auto applied = apply_patches.apply_patch(apply);
    ASSERT_TRUE(applied.ok());

    std::vector<std::string> calls;
    PatchUseCases revert_patches(resolver, CommandPipeline(CommandPipelineHooks{
        [&](const CommandDescriptor& command) {
            EXPECT_EQ(command.action, "patch.revert");
            calls.push_back("validate");
            return AppResult<void>::success();
        },
        [&](const CommandDescriptor&) {
            calls.push_back("authorize");
            return AppResult<void>::success();
        },
        [&](const CommandDescriptor&) {
            calls.push_back("checkpoint");
            return AppResult<void>::success();
        },
        [&](const CommandDescriptor&, const AppError* error) {
            calls.push_back(error ? "audit:error" : "audit:success");
        }}));

    ben_gear::application::PatchRevertCommand revert;
    revert.request.username = std::string("alice");
    revert.request.workspace_name = std::string("default");
    revert.request.session_id = std::string("sid-1");
    revert.change_id = applied.value().change_id;

    auto result = revert_patches.revert_patch(revert);

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().change_id, applied.value().change_id);
    ASSERT_EQ(result.value().reverted_files.size(), static_cast<size_t>(1));
    EXPECT_EQ(result.value().reverted_files[0], "hello.txt");
    EXPECT_EQ(calls, (std::vector<std::string>{"validate", "authorize", "checkpoint", "audit:success"}));
    std::ifstream file(project_dir / "hello.txt", std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "old\n");
}

TEST(ApplicationResult, CarriesValueOrError) {
    auto success = AppResult<int>::success(42);
    ASSERT_TRUE(success.ok());
    EXPECT_EQ(success.value(), 42);

    auto failure = AppResult<int>::failure(AppError::invalid_argument(std::string("bad_input"), std::string("bad input")));
    ASSERT_FALSE(failure.ok());
    EXPECT_EQ(failure.error().code, "bad_input");
}

TEST(CommandPipeline, RunsStagesInOrderAndAuditsSuccess) {
    std::vector<std::string> calls;
    CommandPipeline pipeline(CommandPipelineHooks{
        [&](const CommandDescriptor&) {
            calls.push_back("validate");
            return AppResult<void>::success();
        },
        [&](const CommandDescriptor&) {
            calls.push_back("authorize");
            return AppResult<void>::success();
        },
        [&](const CommandDescriptor&) {
            calls.push_back("checkpoint");
            return AppResult<void>::success();
        },
        [&](const CommandDescriptor&, const AppError* error) {
            calls.push_back(error ? "audit:error" : "audit:success");
        }});

    CommandDescriptor command;
    command.action = std::string("architecture.test");

    auto result = pipeline.execute<int>(command, [&]() {
        calls.push_back("execute");
        return AppResult<int>::success(7);
    });

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value(), 7);
    ASSERT_EQ(calls.size(), static_cast<size_t>(5));
    EXPECT_EQ(calls[0], "validate");
    EXPECT_EQ(calls[1], "authorize");
    EXPECT_EQ(calls[2], "checkpoint");
    EXPECT_EQ(calls[3], "execute");
    EXPECT_EQ(calls[4], "audit:success");
}

TEST(CommandPipeline, StopsBeforeExecutionWhenAuthorizationFails) {
    std::vector<std::string> calls;
    CommandPipeline pipeline(CommandPipelineHooks{
        [&](const CommandDescriptor&) {
            calls.push_back("validate");
            return AppResult<void>::success();
        },
        [&](const CommandDescriptor&) {
            calls.push_back("authorize");
            return AppResult<void>::failure(AppError::permission_denied(std::string("denied"), std::string("denied")));
        },
        [&](const CommandDescriptor&) {
            calls.push_back("checkpoint");
            return AppResult<void>::success();
        },
        [&](const CommandDescriptor&, const AppError*) {
            calls.push_back("audit");
        }});

    CommandDescriptor command;
    command.action = std::string("architecture.denied");

    auto result = pipeline.execute<int>(command, [&]() {
        calls.push_back("execute");
        return AppResult<int>::success(1);
    });

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, "denied");
    ASSERT_EQ(calls.size(), static_cast<size_t>(3));
    EXPECT_EQ(calls[0], "validate");
    EXPECT_EQ(calls[1], "authorize");
    EXPECT_EQ(calls[2], "audit");
}

TEST_F(ApplicationArchitectureTest, SafeCodeChangeServiceRunsPatchGitAndTestLoopWithEvents) {
    auto project_dir = dir() / "project";
    std::filesystem::create_directories(project_dir);
    {
        std::ofstream file(project_dir / "hello.txt", std::ios::binary | std::ios::trunc);
        file << "old\n";
    }
#ifdef _WIN32
    auto git_init_rc = std::system(("git -C \"" + project_dir.string() + "\" init >NUL 2>&1").c_str());
    std::system(("git -C \"" + project_dir.string() + "\" config core.autocrlf false 2>&1").c_str());
#else
    auto git_init_rc = std::system(("git -C '" + project_dir.string() + "' init >/dev/null 2>&1").c_str());
#endif
    EXPECT_EQ(git_init_rc, 0);

    WorkspaceResolver resolver(WorkspaceResolverConfig{dir(), std::string("default"), project_dir.string()});
    std::vector<ben_gear::core::RuntimeEvent> events;
    ben_gear::application::SafeCodeChangeService service(
        resolver,
        CommandPipeline(CommandPipelineHooks{
            {},
            [](const CommandDescriptor&) { return AppResult<void>::success(); },
            [](const CommandDescriptor&) { return AppResult<void>::success(); },
            {},
            {},
            [&](const ben_gear::core::RuntimeEvent& event) { events.push_back(event); }}));

    ben_gear::application::SafeCodeChangeCommand command;
    command.request.username = std::string("alice");
    command.request.workspace_name = std::string("default");
    command.request.session_id = std::string("sid-1");
    command.unified_diff = "--- a/hello.txt\n+++ b/hello.txt\n@@ -1 +1 @@\n-old\n+new\n";
    command.description = "safe update";
#ifdef _WIN32
    command.test_command = "cmd /c \"findstr /b new hello.txt\"";
#else
    command.test_command = "test \"$(cat hello.txt)\" = \"new\"";
#endif
    command.test_cwd = ".";
    command.test_timeout_seconds = 5;

    auto result = service.run(command);

    ASSERT_TRUE(result.ok());
    EXPECT_TRUE(result.value().success);
    EXPECT_FALSE(result.value().checkpoint.checkpoint_id.empty());
    EXPECT_FALSE(result.value().patch_apply.change_id.empty());
    EXPECT_TRUE(result.value().test_run.success);
    EXPECT_EQ(result.value().git_diff.stat, true);
    std::ifstream file(project_dir / "hello.txt", std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "new");

    std::vector<std::string> sequence;
    for (const auto& event : events) {
        if (event.kind == ben_gear::core::RuntimeEventKind::step_started ||
            event.kind == ben_gear::core::RuntimeEventKind::step_succeeded) {
            sequence.push_back(event.step_id + ":" + ben_gear::core::to_string(event.kind));
        }
    }
    ASSERT_GE(sequence.size(), static_cast<size_t>(10));
    EXPECT_EQ(sequence[0], "validate:step_started");
    EXPECT_EQ(sequence[1], "validate:step_succeeded");
    EXPECT_EQ(sequence[2], "authorize:step_started");
    EXPECT_EQ(sequence[3], "authorize:step_succeeded");
    EXPECT_EQ(sequence[4], "checkpoint:step_started");
    EXPECT_EQ(sequence[5], "checkpoint:step_succeeded");
}

TEST_F(ApplicationArchitectureTest, SafeCodeChangeServiceStopsBeforeWriteWhenPermissionDenied) {
    auto project_dir = dir() / "project";
    std::filesystem::create_directories(project_dir);
    {
        std::ofstream file(project_dir / "hello.txt", std::ios::binary | std::ios::trunc);
        file << "old\n";
    }

    WorkspaceResolver resolver(WorkspaceResolverConfig{dir(), std::string("default"), project_dir.string()});
    ben_gear::application::SafeCodeChangeService service(
        resolver,
        CommandPipeline(CommandPipelineHooks{
            {},
            [](const CommandDescriptor&) {
                return AppResult<void>::failure(AppError::permission_denied(std::string("permission_denied"), std::string("denied")));
            }}));

    ben_gear::application::SafeCodeChangeCommand command;
    command.request.username = std::string("alice");
    command.request.workspace_name = std::string("default");
    command.request.session_id = std::string("sid-1");
    command.unified_diff = "--- a/hello.txt\n+++ b/hello.txt\n@@ -1 +1 @@\n-old\n+new\n";

    auto result = service.run(command);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, "permission_denied");
    std::ifstream file(project_dir / "hello.txt", std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    EXPECT_EQ(content, "old\n");
}

TEST_F(ApplicationArchitectureTest, SafeCodeChangeServiceReturnsPatchApplyFailure) {
    auto project_dir = dir() / "project";
    std::filesystem::create_directories(project_dir);
    {
        std::ofstream file(project_dir / "hello.txt", std::ios::binary | std::ios::trunc);
        file << "actual\n";
    }

    WorkspaceResolver resolver(WorkspaceResolverConfig{dir(), std::string("default"), project_dir.string()});
    ben_gear::application::SafeCodeChangeService service(
        resolver,
        CommandPipeline(CommandPipelineHooks{{}, [](const CommandDescriptor&) { return AppResult<void>::success(); }}));

    ben_gear::application::SafeCodeChangeCommand command;
    command.request.username = std::string("alice");
    command.request.workspace_name = std::string("default");
    command.request.session_id = std::string("sid-1");
    command.unified_diff = "--- a/hello.txt\n+++ b/hello.txt\n@@ -1 +1 @@\n-old\n+new\n";

    auto result = service.run(command);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, "patch_conflict");
    EXPECT_NE(result.error().details_json.find("apply_patch"), std::string::npos);
}

TEST_F(ApplicationArchitectureTest, SafeCodeChangeServicePreservesTestDiagnosticsOnFailure) {
    auto project_dir = dir() / "project";
    std::filesystem::create_directories(project_dir);
    {
        std::ofstream file(project_dir / "hello.txt", std::ios::binary | std::ios::trunc);
        file << "old\n";
    }

    WorkspaceResolver resolver(WorkspaceResolverConfig{dir(), std::string("default"), project_dir.string()});
    ben_gear::application::SafeCodeChangeService service(
        resolver,
        CommandPipeline(CommandPipelineHooks{{}, [](const CommandDescriptor&) { return AppResult<void>::success(); }}));

    ben_gear::application::SafeCodeChangeCommand command;
    command.request.username = std::string("alice");
    command.request.workspace_name = std::string("default");
    command.request.session_id = std::string("sid-1");
    command.unified_diff = "--- a/hello.txt\n+++ b/hello.txt\n@@ -1 +1 @@\n-old\n+new\n";
#ifdef _WIN32
    command.test_command = "echo test failed: expected old && exit /b 2";
#else
    command.test_command = "printf 'test failed: expected old\\n' && exit 2";
#endif
    command.test_timeout_seconds = 5;

    auto result = service.run(command);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(result.error().code, "test_failed");
    auto details = ben_gear::Json::parse(result.error().details_json);
    EXPECT_EQ(details.value("stage", ""), "test_loop");
    EXPECT_FALSE(details.value("checkpoint_id", "").empty());
    EXPECT_FALSE(details.value("change_id", "").empty());
    EXPECT_EQ(details["test_run"].value("failure_category", ""), "test");
}
