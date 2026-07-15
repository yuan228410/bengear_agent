#include "application/safe_code_change_service.hpp"
#include "application/command_pipeline.hpp"
#include "application/workspace_resolver.hpp"
#include "intelligence/code_intel/code_intelligence_index.hpp"
#include "test_framework.hpp"

#include "test_util.hpp"

#include <fstream>

using bengear::test::TmpDirTest;

class RepoIntelligenceTest : public TmpDirTest {};

namespace {

using ben_gear::application::CommandDescriptor;
using ben_gear::application::CommandPipeline;
using ben_gear::application::CommandPipelineHooks;
using ben_gear::application::RequestContext;
using ben_gear::application::WorkspaceResolver;
using ben_gear::application::WorkspaceResolverConfig;
using ben_gear::domain::AppResult;
using ben_gear::domain::AppError;

} // namespace

TEST_F(RepoIntelligenceTest, SafeCodeChangeServicePopulatesRepoIntelligenceWhenInjected) {
    auto project_dir = dir() / "project";
    std::filesystem::create_directories(project_dir);
    {
        std::ofstream file(project_dir / "app.hpp", std::ios::binary | std::ios::trunc);
        file << "class App { public: void run(); };\n";
    }
    {
        std::ofstream file(project_dir / "app.cpp", std::ios::binary | std::ios::trunc);
        file << "#include \"app.hpp\"\nvoid App::run() {}\n";
    }
    {
        std::ofstream file(project_dir / "test_app.cpp", std::ios::binary | std::ios::trunc);
        file << "#include \"app.hpp\"\nint main() { return 0; }\n";
    }

    WorkspaceResolver resolver(WorkspaceResolverConfig{dir(), std::string("default"), project_dir.string()});

    auto ws_ctx = resolver.resolve(RequestContext{std::string("alice"), std::string("default"), std::string("sid-1")}).value().to_workspace_context();
    auto code_intelligence = std::make_shared<ben_gear::code_intel::CodeIntelligenceIndex>(ws_ctx);

    ben_gear::application::SafeCodeChangeService service(
        resolver,
        CommandPipeline(CommandPipelineHooks{
            {},
            [](const CommandDescriptor&) { return AppResult<void>::success(); },
            [](const CommandDescriptor&) { return AppResult<void>::success(); },
            {},
            {},
            {}}),
        {},
        code_intelligence);

    ben_gear::application::SafeCodeChangeCommand command;
    command.request.username = std::string("alice");
    command.request.workspace_name = std::string("default");
    command.request.session_id = std::string("sid-1");
    command.unified_diff = "--- a/app.hpp\n+++ b/app.hpp\n@@ -1 +1 @@\n-class App { public: void run(); };\n+class App { public: void run(); int x; };\n";
    command.description = "add member";

    auto result = service.run(command);

    ASSERT_TRUE(result.ok());
    EXPECT_TRUE(result.value().success);
    EXPECT_FALSE(result.value().repo_intelligence.empty());
    EXPECT_TRUE(result.value().repo_intelligence.value("success", false));
    EXPECT_TRUE(result.value().repo_intelligence.contains("affected_paths"));
    EXPECT_TRUE(result.value().repo_intelligence["affected_paths"].is_array());
    EXPECT_EQ(result.value().repo_intelligence["affected_paths"].size(), static_cast<size_t>(1));
    EXPECT_EQ(result.value().repo_intelligence["affected_paths"][0].get<std::string>(), "app.hpp");
    EXPECT_TRUE(result.value().repo_intelligence.contains("symbols"));
    EXPECT_TRUE(result.value().repo_intelligence["symbols"].is_array());
    EXPECT_TRUE(result.value().repo_intelligence.contains("impacts"));
    EXPECT_TRUE(result.value().repo_intelligence.contains("related_tests"));
    EXPECT_TRUE(result.value().repo_intelligence.contains("test_suggestions"));
}

TEST_F(RepoIntelligenceTest, SafeCodeChangeServiceWorksWhenCodeIntelligenceNotInjected) {
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
            [](const CommandDescriptor&) { return AppResult<void>::success(); },
            [](const CommandDescriptor&) { return AppResult<void>::success(); },
            {},
            {},
            {}}));

    ben_gear::application::SafeCodeChangeCommand command;
    command.request.username = std::string("alice");
    command.request.workspace_name = std::string("default");
    command.request.session_id = std::string("sid-1");
    command.unified_diff = "--- a/hello.txt\n+++ b/hello.txt\n@@ -1 +1 @@\n-old\n+new\n";
    command.description = "update";

    auto result = service.run(command);

    ASSERT_TRUE(result.ok());
    EXPECT_TRUE(result.value().success);
    EXPECT_TRUE(result.value().repo_intelligence.empty());
}
