#include "intelligence/code_intel/code_intel_service.hpp"
#include "intelligence/repo_map/repo_map_service.hpp"
#include "test_framework.hpp"
#include "intelligence/workspace_index/workspace_index_service.hpp"

#include "test_util.hpp"

#include <filesystem>
#include <fstream>
#include <string_view>

using bengear::test::TmpDirTest;

class WorkspaceIndexServiceTest : public TmpDirTest {};

namespace {

void write_text(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

ben_gear::workspace::WorkspaceContext make_ctx(const std::filesystem::path& root) {
    ben_gear::workspace::WorkspaceContext ctx;
    ctx.project_path = root.string();
    ctx.workspace_name = root.filename().string();
    ctx.session_id = std::string("workspace-index-test");
    ctx.tier_paths.user_dir = root / ".bengear-test-user";
    return ctx;
}

ben_gear::repo_map::RepoMapService::Options tiny_options() {
    ben_gear::repo_map::RepoMapService::Options options;
    options.max_files = 100;
    options.max_symbols = 100;
    options.max_dependencies = 100;
    return options;
}

ben_gear::Json repo_map_result_json(const ben_gear::domain::AppResult<ben_gear::repo_map::RepoMapFindSymbolsResult>& result) {
    return result.ok() ? ben_gear::repo_map::to_json(result.value())
                       : ben_gear::Json{{"success", false}, {"error_type", result.error().code}, {"message", result.error().message}};
}

} // namespace

TEST_F(WorkspaceIndexServiceTest, RepoMapReusesWorkspaceIndexSnapshot) {
    write_text(dir() / "src/app.cpp", "int first() { return 1; }\n");
    auto ctx = make_ctx(dir());
    auto index_service = std::make_shared<ben_gear::workspace_index::WorkspaceIndexService>(ctx);
    ben_gear::repo_map::RepoMapService service(ctx, index_service);

    auto first = service.snapshot(tiny_options());
    auto second = service.snapshot(tiny_options());

    ASSERT_TRUE(first.success);
    ASSERT_TRUE(second.success);
    auto metrics = index_service->metrics();
    EXPECT_EQ(metrics.index_build_count, 1);
    EXPECT_EQ(metrics.cache_hit_count, 1);
    EXPECT_EQ(metrics.cache_miss_count, 1);
}

TEST_F(WorkspaceIndexServiceTest, RefreshForcesRebuild) {
    write_text(dir() / "src/app.cpp", "int first() { return 1; }\n");
    auto ctx = make_ctx(dir());
    auto index_service = std::make_shared<ben_gear::workspace_index::WorkspaceIndexService>(ctx);
    ben_gear::repo_map::RepoMapService service(ctx, index_service);

    auto options = tiny_options();
    ASSERT_TRUE(service.snapshot(options).success);
    options.refresh = true;
    ASSERT_TRUE(service.snapshot(options).success);

    auto metrics = index_service->metrics();
    EXPECT_EQ(metrics.index_build_count, 2);
    EXPECT_EQ(metrics.cache_miss_count, 2);
    EXPECT_EQ(metrics.invalidated_count, 1);
}

TEST_F(WorkspaceIndexServiceTest, FileChangeInvalidatesCacheSignature) {
    write_text(dir() / "src/app.cpp", "int first() { return 1; }\n");
    auto ctx = make_ctx(dir());
    auto index_service = std::make_shared<ben_gear::workspace_index::WorkspaceIndexService>(ctx);
    ben_gear::repo_map::RepoMapService service(ctx, index_service);

    ASSERT_TRUE(service.snapshot(tiny_options()).success);
    write_text(dir() / "src/app.cpp", "int first() { return 1; }\nint second() { return 2; }\n");
    auto changed = repo_map_result_json(service.find_symbols("second", "function", "cpp", 10, tiny_options()));

    ASSERT_TRUE(changed.value("success", false));
    ASSERT_EQ(changed["symbols"].size(), 1u);
    auto metrics = index_service->metrics();
    EXPECT_EQ(metrics.index_build_count, 2);
    EXPECT_EQ(metrics.cache_miss_count, 2);
}

TEST_F(WorkspaceIndexServiceTest, RequestSessionReusesIndexAcrossCodeIntelCalls) {
    write_text(dir() / "include/app.hpp", "class App { public: void run(); };\n");
    write_text(dir() / "src/app.cpp", "#include \"app.hpp\"\nvoid use() { App app; }\n");
    auto ctx = make_ctx(dir());
    auto index_service = std::make_shared<ben_gear::workspace_index::WorkspaceIndexService>(ctx);
    auto repo_service = std::make_shared<ben_gear::repo_map::RepoMapService>(ctx, index_service);
    ben_gear::code_intel::CodeIntelService code_service(ctx, repo_service);
    auto request_session = code_service.request_session();

    auto symbols = code_service.document_symbols("include/app.hpp", request_session);
    ben_gear::code_intel::CodeIntelQuery query;
    query.symbol = "App";
    auto definitions = code_service.definition(query, ben_gear::code_intel::CodeIntelOptions{}, request_session);

    ASSERT_TRUE(symbols.ok());
    ASSERT_TRUE(definitions.ok());
    EXPECT_EQ(index_service->metrics().index_build_count, 1);
    EXPECT_EQ(index_service->metrics().cache_miss_count, 1);
}

TEST_F(WorkspaceIndexServiceTest, WorkspaceCachesAreIsolated) {
    auto one = dir() / "one";
    auto two = dir() / "two";
    write_text(one / "src/app.cpp", "int one_symbol() { return 1; }\n");
    write_text(two / "src/app.cpp", "int two_symbol() { return 2; }\n");

    auto ctx_one = make_ctx(one);
    auto ctx_two = make_ctx(two);
    auto index_one = std::make_shared<ben_gear::workspace_index::WorkspaceIndexService>(ctx_one);
    auto index_two = std::make_shared<ben_gear::workspace_index::WorkspaceIndexService>(ctx_two);
    ben_gear::repo_map::RepoMapService service_one(ctx_one, index_one);
    ben_gear::repo_map::RepoMapService service_two(ctx_two, index_two);

    auto symbols_one = repo_map_result_json(service_one.find_symbols("one_symbol", "function", "cpp", 10, tiny_options()));
    auto symbols_two = repo_map_result_json(service_two.find_symbols("two_symbol", "function", "cpp", 10, tiny_options()));

    ASSERT_TRUE(symbols_one.value("success", false));
    ASSERT_TRUE(symbols_two.value("success", false));
    ASSERT_EQ(symbols_one["symbols"].size(), 1u);
    ASSERT_EQ(symbols_two["symbols"].size(), 1u);
    EXPECT_EQ(index_one->metrics().index_build_count, 1);
    EXPECT_EQ(index_two->metrics().index_build_count, 1);
}

#include "intelligence/code_intel/code_intelligence_index.hpp"

TEST_F(WorkspaceIndexServiceTest, CodeIntelligenceIndexSharesSnapshotAcrossRepoMapAndCodeIntelQueries) {
    write_text(dir() / "include/app.hpp", "class App { public: void run(); };\n");
    write_text(dir() / "src/app.cpp", "#include \"app.hpp\"\nvoid use() { App app; app.run(); }\n");

    auto ctx = make_ctx(dir());
    auto index_service = std::make_shared<ben_gear::workspace_index::WorkspaceIndexService>(ctx);
    auto repo_service = std::make_shared<ben_gear::repo_map::RepoMapService>(ctx, index_service);
    auto code_service = std::make_shared<ben_gear::code_intel::CodeIntelService>(ctx, repo_service);
    ben_gear::code_intel::CodeIntelligenceIndex intelligence(ctx, repo_service, code_service);

    ben_gear::repo_map::RepoMapService::Options shared_options = tiny_options();
    shared_options.max_dependencies = 0;
    ben_gear::code_intel::CodeIntelOptions code_options;
    code_options.max_files = shared_options.max_files;
    code_options.max_symbols = shared_options.max_symbols;
    code_options.max_file_bytes = shared_options.max_file_bytes;

    auto overview = intelligence.overview(shared_options);
    auto symbols = intelligence.workspace_symbols("App", "class", "cpp", 10, code_options);
    ben_gear::code_intel::CodeIntelQuery query;
    query.symbol = "App";
    auto definitions = intelligence.definition(query, code_options);
    query.limit = 10;
    auto references = intelligence.references(query, code_options);

    ASSERT_TRUE(overview.ok());
    ASSERT_TRUE(symbols.ok());
    ASSERT_TRUE(definitions.ok());
    ASSERT_TRUE(references.ok());
    EXPECT_FALSE(overview.value().important_files.empty());
    EXPECT_EQ(symbols.value().symbols.front().symbol, "App");
    EXPECT_EQ(definitions.value().definitions.front().symbol, "App");
    EXPECT_FALSE(references.value().references.empty());
    EXPECT_EQ(index_service->metrics().index_build_count, 1);
    EXPECT_EQ(index_service->metrics().cache_miss_count, 1);
}

TEST_F(WorkspaceIndexServiceTest, CodeIntelligenceIndexExplainsPathFromSharedSnapshot) {
    write_text(dir() / "include/app.hpp", "class App { public: void run(); };\n");
    write_text(dir() / "src/app.cpp", "#include \"app.hpp\"\nvoid use() { App app; }\n");

    auto ctx = make_ctx(dir());
    auto index_service = std::make_shared<ben_gear::workspace_index::WorkspaceIndexService>(ctx);
    auto repo_service = std::make_shared<ben_gear::repo_map::RepoMapService>(ctx, index_service);
    ben_gear::code_intel::CodeIntelligenceIndex intelligence(ctx, repo_service);

    auto explained = intelligence.explain_path("include/app.hpp", tiny_options());
    auto files = intelligence.find_files("app", "header", "cpp", 10, tiny_options());

    ASSERT_TRUE(explained.ok());
    ASSERT_TRUE(files.ok());
    EXPECT_EQ(explained.value().file.path, "include/app.hpp");
    EXPECT_FALSE(explained.value().symbols.empty());
    EXPECT_FALSE(files.value().files.empty());
    EXPECT_EQ(index_service->metrics().index_build_count, 1);
}
