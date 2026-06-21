#include "ben_gear/repo_map/repo_map_service.hpp"
#include "ben_gear/test/test_framework.hpp"
#include "ben_gear/workspace_index/workspace_index_service.hpp"

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
    ctx.project_path = ben_gear::base::container::String(root.string().c_str());
    ctx.workspace_name = ben_gear::base::container::String(root.filename().string().c_str());
    ctx.session_id = ben_gear::base::container::String("workspace-index-test");
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

} // namespace

TEST_F(WorkspaceIndexServiceTest, RepoMapReusesWorkspaceIndexSnapshot) {
    write_text(dir() / "src/app.cpp", "int first() { return 1; }\n");
    auto ctx = make_ctx(dir());
    auto index_service = std::make_shared<ben_gear::workspace_index::WorkspaceIndexService>(ctx);
    ben_gear::repo_map::RepoMapService service(ctx, nullptr, nullptr, index_service);

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
    ben_gear::repo_map::RepoMapService service(ctx, nullptr, nullptr, index_service);

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
    ben_gear::repo_map::RepoMapService service(ctx, nullptr, nullptr, index_service);

    ASSERT_TRUE(service.snapshot(tiny_options()).success);
    write_text(dir() / "src/app.cpp", "int first() { return 1; }\nint second() { return 2; }\n");
    auto changed = service.find_symbols("second", "function", "cpp", 10, tiny_options());

    ASSERT_TRUE(changed.value("success", false));
    ASSERT_EQ(changed["symbols"].size(), 1u);
    auto metrics = index_service->metrics();
    EXPECT_EQ(metrics.index_build_count, 2);
    EXPECT_EQ(metrics.cache_miss_count, 2);
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
    ben_gear::repo_map::RepoMapService service_one(ctx_one, nullptr, nullptr, index_one);
    ben_gear::repo_map::RepoMapService service_two(ctx_two, nullptr, nullptr, index_two);

    auto symbols_one = service_one.find_symbols("one_symbol", "function", "cpp", 10, tiny_options());
    auto symbols_two = service_two.find_symbols("two_symbol", "function", "cpp", 10, tiny_options());

    ASSERT_TRUE(symbols_one.value("success", false));
    ASSERT_TRUE(symbols_two.value("success", false));
    ASSERT_EQ(symbols_one["symbols"].size(), 1u);
    ASSERT_EQ(symbols_two["symbols"].size(), 1u);
    EXPECT_EQ(index_one->metrics().index_build_count, 1);
    EXPECT_EQ(index_two->metrics().index_build_count, 1);
}
