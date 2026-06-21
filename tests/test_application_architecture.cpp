#include "ben_gear/application/command_pipeline.hpp"
#include "ben_gear/application/patch_use_cases.hpp"
#include "ben_gear/application/workspace_resolver.hpp"
#include "ben_gear/test/test_framework.hpp"
#include "ben_gear/workspace/manager.hpp"

#include "test_util.hpp"

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
using ben_gear::base::container::String;
using ben_gear::domain::AppError;
using ben_gear::domain::AppResult;

class ApplicationArchitectureTest : public TmpDirTest {};

} // namespace

TEST_F(ApplicationArchitectureTest, WorkspaceResolverBuildsResolvedWorkspaceContext) {
    WorkspaceResolver resolver(WorkspaceResolverConfig{dir(), String("default"), String("/fallback/project")});

    RequestContext request;
    request.username = String("alice");
    request.workspace_name = String("project1");
    request.session_id = String("session-1");

    auto result = resolver.resolve(request);

    ASSERT_TRUE(result.ok());
    const auto& resolved = result.value();
    EXPECT_EQ(std::string(resolved.request.username.c_str()), "alice");
    EXPECT_EQ(std::string(resolved.request.workspace_name.c_str()), "project1");
    EXPECT_EQ(resolved.user_dir, dir() / "users" / "alice");
    EXPECT_EQ(resolved.workspace_dir, dir() / "users" / "alice" / "workspaces" / "project1");
    EXPECT_EQ(std::string(resolved.project_path.c_str()), "/fallback/project");

    auto ws_ctx = resolved.to_workspace_context();
    EXPECT_EQ(std::string(ws_ctx.username.c_str()), "alice");
    EXPECT_EQ(std::string(ws_ctx.workspace_name.c_str()), "project1");
    EXPECT_EQ(std::string(ws_ctx.session_id.c_str()), "session-1");
}

TEST_F(ApplicationArchitectureTest, WorkspaceResolverUsesDefaultWorkspaceAndStoredProjectPath) {
    WorkspaceResolver resolver(WorkspaceResolverConfig{dir(), String("default"), String("/fallback/project")});
    ben_gear::workspace::WorkspaceManager manager(dir() / "users" / "bob");
    auto created = manager.create(String("code"), String("/repo/code"));
    ASSERT_TRUE(created.has_value());

    RequestContext request;
    request.username = String("bob");
    request.workspace_name = String("code");

    auto result = resolver.resolve(request);

    ASSERT_TRUE(result.ok());
    EXPECT_EQ(std::string(result.value().request.workspace_name.c_str()), "code");
    EXPECT_EQ(std::string(result.value().project_path.c_str()), "/repo/code");

    request.workspace_name = String();
    auto default_result = resolver.resolve(request);
    ASSERT_TRUE(default_result.ok());
    EXPECT_EQ(std::string(default_result.value().request.workspace_name.c_str()), "default");
}

TEST_F(ApplicationArchitectureTest, PatchUseCaseReturnsTypedPreview) {
    auto project_dir = dir() / "project";
    std::filesystem::create_directories(project_dir);
    {
        std::ofstream file(project_dir / "hello.txt", std::ios::binary | std::ios::trunc);
        file << "old\n";
    }

    WorkspaceResolver resolver(WorkspaceResolverConfig{dir(), String("default"), String(project_dir.string().c_str())});
    PatchUseCases patches(resolver);

    PatchPreviewQuery query;
    query.request.username = String("alice");
    query.request.workspace_name = String("default");
    query.request.session_id = String("sid-1");
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

    WorkspaceResolver resolver(WorkspaceResolverConfig{dir(), String("default"), String(project_dir.string().c_str())});
    std::vector<std::string> calls;
    PatchUseCases patches(resolver, CommandPipeline(CommandPipelineHooks{
        [&](const CommandDescriptor& command) {
            EXPECT_EQ(std::string(command.action.c_str()), "patch.apply");
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
    command.request.username = String("alice");
    command.request.workspace_name = String("default");
    command.request.session_id = String("sid-1");
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

    WorkspaceResolver resolver(WorkspaceResolverConfig{dir(), String("default"), String(project_dir.string().c_str())});
    std::vector<std::string> calls;
    PatchUseCases patches(resolver, CommandPipeline(CommandPipelineHooks{
        [&](const CommandDescriptor&) {
            calls.push_back("validate");
            return AppResult<void>::success();
        },
        [&](const CommandDescriptor&) {
            calls.push_back("authorize");
            return AppResult<void>::failure(AppError::permission_denied(String("denied"), String("denied")));
        },
        [&](const CommandDescriptor&) {
            calls.push_back("checkpoint");
            return AppResult<void>::success();
        },
        [&](const CommandDescriptor&, const AppError*) {
            calls.push_back("audit");
        }}));

    ben_gear::application::PatchApplyCommand command;
    command.request.username = String("alice");
    command.request.workspace_name = String("default");
    command.request.session_id = String("sid-1");
    command.unified_diff = "--- a/hello.txt\n+++ b/hello.txt\n@@ -1 +1 @@\n-old\n+new\n";

    auto result = patches.apply_patch(command);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(std::string(result.error().code.c_str()), "denied");
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

    WorkspaceResolver resolver(WorkspaceResolverConfig{dir(), String("default"), String(project_dir.string().c_str())});
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
            return AppResult<void>::failure(AppError::unavailable(String("checkpoint_failed"), String("checkpoint failed")));
        },
        [&](const CommandDescriptor&, const AppError*) {
            calls.push_back("audit");
        }}));

    ben_gear::application::PatchApplyCommand command;
    command.request.username = String("alice");
    command.request.workspace_name = String("default");
    command.request.session_id = String("sid-1");
    command.unified_diff = "--- a/hello.txt\n+++ b/hello.txt\n@@ -1 +1 @@\n-old\n+new\n";

    auto result = patches.apply_patch(command);

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(std::string(result.error().code.c_str()), "checkpoint_failed");
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

    WorkspaceResolver resolver(WorkspaceResolverConfig{dir(), String("default"), String(project_dir.string().c_str())});
    PatchUseCases apply_patches(resolver);
    ben_gear::application::PatchApplyCommand apply;
    apply.request.username = String("alice");
    apply.request.workspace_name = String("default");
    apply.request.session_id = String("sid-1");
    apply.unified_diff = "--- a/hello.txt\n+++ b/hello.txt\n@@ -1 +1 @@\n-old\n+new\n";
    auto applied = apply_patches.apply_patch(apply);
    ASSERT_TRUE(applied.ok());

    std::vector<std::string> calls;
    PatchUseCases revert_patches(resolver, CommandPipeline(CommandPipelineHooks{
        [&](const CommandDescriptor& command) {
            EXPECT_EQ(std::string(command.action.c_str()), "patch.revert");
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
    revert.request.username = String("alice");
    revert.request.workspace_name = String("default");
    revert.request.session_id = String("sid-1");
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

    auto failure = AppResult<int>::failure(AppError::invalid_argument(String("bad_input"), String("bad input")));
    ASSERT_FALSE(failure.ok());
    EXPECT_EQ(std::string(failure.error().code.c_str()), "bad_input");
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
    command.action = String("architecture.test");

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
            return AppResult<void>::failure(AppError::permission_denied(String("denied"), String("denied")));
        },
        [&](const CommandDescriptor&) {
            calls.push_back("checkpoint");
            return AppResult<void>::success();
        },
        [&](const CommandDescriptor&, const AppError*) {
            calls.push_back("audit");
        }});

    CommandDescriptor command;
    command.action = String("architecture.denied");

    auto result = pipeline.execute<int>(command, [&]() {
        calls.push_back("execute");
        return AppResult<int>::success(1);
    });

    ASSERT_FALSE(result.ok());
    EXPECT_EQ(std::string(result.error().code.c_str()), "denied");
    ASSERT_EQ(calls.size(), static_cast<size_t>(3));
    EXPECT_EQ(calls[0], "validate");
    EXPECT_EQ(calls[1], "authorize");
    EXPECT_EQ(calls[2], "audit");
}
