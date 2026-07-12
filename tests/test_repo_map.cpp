#include "intelligence/repo_map/repo_map_service.hpp"
#include "tool/repo_map_tools.hpp"
#include "tool/registry.hpp"
#include "test_framework.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string_view>

using bengear::test::TmpDirTest;

class RepoMapServiceTest : public TmpDirTest {};

namespace {

void run_cmd(const std::filesystem::path& cwd, const std::string& command) {
    int rc;
#ifdef _WIN32
    if (command.rfind("git ", 0) == 0) {
        rc = std::system(("git -C \"" + cwd.string() + "\" " + command.substr(4) + " 2>&1").c_str());
    } else {
        rc = std::system(("cd /d \"" + cwd.string() + "\" && " + command + " 2>&1").c_str());
    }
#else
    rc = std::system(("cd '" + cwd.string() + "' && " + command + " >/dev/null 2>&1").c_str());
#endif
    ASSERT_EQ(rc, 0);
}

void write_text(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

ben_gear::workspace::WorkspaceContext make_ctx(const std::filesystem::path& root) {
    ben_gear::workspace::WorkspaceContext ctx;
    ctx.project_path = ben_gear::base::container::String(root.string().c_str());
    ctx.session_id = ben_gear::base::container::String("repo-map-test-session");
    ctx.tier_paths.user_dir = root / ".bengear-test-user";
    return ctx;
}

bool array_contains_path(const ben_gear::Json& array, const std::string& path) {
    if (!array.is_array()) return false;
    for (const auto& item : array) {
        if (item.is_string() && item.get<std::string>() == path) return true;
        if (item.is_object() && item.value("path", "") == path) return true;
    }
    return false;
}

bool has_symbol(const ben_gear::Json& symbols, const std::string& name, const std::string& kind = {}) {
    if (!symbols.is_array()) return false;
    for (const auto& symbol : symbols) {
        if (symbol.value("name", "") != name) continue;
        if (!kind.empty() && symbol.value("kind", "") != kind) continue;
        return true;
    }
    return false;
}

ben_gear::Json repo_map_result_json(const ben_gear::domain::AppResult<ben_gear::repo_map::RepoMapOverviewResult>& result) {
    return result.ok() ? ben_gear::repo_map::to_json(result.value())
                       : ben_gear::Json{{"success", false}, {"error_type", std::string(result.error().code.c_str())}, {"message", std::string(result.error().message.c_str())}};
}

ben_gear::Json repo_map_result_json(const ben_gear::domain::AppResult<ben_gear::repo_map::RepoMapFindFilesResult>& result) {
    return result.ok() ? ben_gear::repo_map::to_json(result.value())
                       : ben_gear::Json{{"success", false}, {"error_type", std::string(result.error().code.c_str())}, {"message", std::string(result.error().message.c_str())}};
}

ben_gear::Json repo_map_result_json(const ben_gear::domain::AppResult<ben_gear::repo_map::RepoMapFindSymbolsResult>& result) {
    return result.ok() ? ben_gear::repo_map::to_json(result.value())
                       : ben_gear::Json{{"success", false}, {"error_type", std::string(result.error().code.c_str())}, {"message", std::string(result.error().message.c_str())}};
}

ben_gear::Json repo_map_result_json(const ben_gear::domain::AppResult<ben_gear::repo_map::RepoMapExplainPathResult>& result) {
    return result.ok() ? ben_gear::repo_map::to_json(result.value())
                       : ben_gear::Json{{"success", false}, {"error_type", std::string(result.error().code.c_str())}, {"message", std::string(result.error().message.c_str())}};
}

void create_basic_project(const std::filesystem::path& root) {
    write_text(root / "CMakeLists.txt", "add_executable(app src/foo.cpp)\n");
    write_text(root / "include/foo.hpp",
               "#pragma once\n"
               "namespace demo {\n"
               "class Foo {\n"
               "public:\n"
               "  void run();\n"
               "};\n"
               "struct Bar {};\n"
               "}\n");
    write_text(root / "src/foo.cpp",
               "#include \"foo.hpp\"\n"
               "namespace demo {\n"
               "int add(int a, int b) { return a + b; }\n"
               "}\n");
    write_text(root / "tests/test_foo.cpp", "#include \"foo.hpp\"\nint main() { return 0; }\n");
    write_text(root / "README.md", "# Demo\n");
}

} // namespace

TEST_F(RepoMapServiceTest, OverviewDetectsProjectShape) {
    create_basic_project(dir());
    auto ctx = make_ctx(dir());
    auto test_loop = std::make_shared<ben_gear::test_loop::TestLoopService>(ctx);
    ben_gear::repo_map::RepoMapService service(ctx, nullptr, test_loop);

    auto overview = repo_map_result_json(service.overview());
    ASSERT_TRUE(overview.value("success", false));
    auto summary = overview["summary"];
    EXPECT_GE(summary.value("indexed_files", 0), 5);
    EXPECT_GT(summary["languages"].value("cpp", 0), 0);
    EXPECT_GT(summary["file_kinds"].value("source", 0), 0);
    EXPECT_GT(summary["file_kinds"].value("header", 0), 0);
    EXPECT_GT(summary["file_kinds"].value("test", 0), 0);
    EXPECT_GT(summary["file_kinds"].value("document", 0), 0);
    EXPECT_FALSE(summary["test_suggestions"].empty());
    EXPECT_TRUE(array_contains_path(summary["top_directories"], "include"));
    EXPECT_TRUE(array_contains_path(summary["top_directories"], "src"));
}

TEST_F(RepoMapServiceTest, ExtractsCppSymbols) {
    create_basic_project(dir());
    ben_gear::repo_map::RepoMapService service(make_ctx(dir()));

    auto symbols = repo_map_result_json(service.find_symbols("Foo"));
    ASSERT_TRUE(symbols.value("success", false));
    EXPECT_TRUE(has_symbol(symbols["symbols"], "Foo", "class"));

    auto all = repo_map_result_json(service.find_symbols("", "struct"));
    ASSERT_TRUE(all.value("success", false));
    EXPECT_TRUE(has_symbol(all["symbols"], "Bar", "struct"));

    auto function = repo_map_result_json(service.find_symbols("add", "function"));
    ASSERT_TRUE(function.value("success", false));
    EXPECT_TRUE(has_symbol(function["symbols"], "add", "function"));
}

TEST_F(RepoMapServiceTest, ExtractsDependencies) {
    create_basic_project(dir());
    ben_gear::repo_map::RepoMapService service(make_ctx(dir()));

    auto explained = repo_map_result_json(service.explain_path("src/foo.cpp"));
    ASSERT_TRUE(explained.value("success", false));
    ASSERT_TRUE(explained["dependencies"].is_array());
    ASSERT_GE(explained["dependencies"].size(), 1u);
    EXPECT_EQ(explained["dependencies"][0].value("target", ""), "foo.hpp");
    EXPECT_TRUE(explained["dependencies"][0].value("resolved", false));
    EXPECT_EQ(explained["dependencies"][0].value("resolved_path", ""), "include/foo.hpp");
}

TEST_F(RepoMapServiceTest, FindFilesFiltersByKindAndLanguage) {
    create_basic_project(dir());
    ben_gear::repo_map::RepoMapService service(make_ctx(dir()));

    auto headers = repo_map_result_json(service.find_files("foo", "header", "cpp", 10));
    ASSERT_TRUE(headers.value("success", false));
    ASSERT_EQ(headers["files"].size(), 1u);
    EXPECT_EQ(headers["files"][0].value("path", ""), "include/foo.hpp");

    auto tests = repo_map_result_json(service.find_files("", "test", "", 10));
    ASSERT_TRUE(tests.value("success", false));
    EXPECT_EQ(tests["files"][0].value("path", ""), "tests/test_foo.cpp");
}

TEST_F(RepoMapServiceTest, FindSymbolsFiltersByQuery) {
    create_basic_project(dir());
    ben_gear::repo_map::RepoMapService service(make_ctx(dir()));

    auto symbols = repo_map_result_json(service.find_symbols("Fo", "class", "cpp", 10));
    ASSERT_TRUE(symbols.value("success", false));
    ASSERT_EQ(symbols["symbols"].size(), 1u);
    EXPECT_EQ(symbols["symbols"][0].value("name", ""), "Foo");
}

TEST_F(RepoMapServiceTest, ExplainPathRejectsWorkspaceEscape) {
    create_basic_project(dir());
    ben_gear::repo_map::RepoMapService service(make_ctx(dir()));

    auto result = repo_map_result_json(service.explain_path("../outside.cpp"));
    EXPECT_FALSE(result.value("success", true));
    EXPECT_EQ(result.value("error_type", ""), "path_outside_workspace");
}

TEST_F(RepoMapServiceTest, SkipsExcludedOrLargeFiles) {
    create_basic_project(dir());
    write_text(dir() / "node_modules/pkg/index.js", "export function noisy() {}\n");
    write_text(dir() / "third_party/lib/lib.cpp", "int external() { return 0; }\n");
    std::string large(2 * 1024 * 1024, 'x');
    write_text(dir() / "src/large.cpp", large);

    ben_gear::repo_map::RepoMapService service(make_ctx(dir()));
    auto overview = repo_map_result_json(service.overview());
    ASSERT_TRUE(overview.value("success", false));
    auto files = repo_map_result_json(service.find_files("", "", "", 100));
    ASSERT_TRUE(files.value("success", false));
    for (const auto& file : files["files"]) {
        EXPECT_NE(file.value("path", ""), "node_modules/pkg/index.js");
        EXPECT_NE(file.value("path", ""), "third_party/lib/lib.cpp");
    }
    auto large_file = repo_map_result_json(service.explain_path("src/large.cpp"));
    ASSERT_TRUE(large_file.value("success", false));
    EXPECT_TRUE(large_file["file"].value("skipped", false));
    EXPECT_EQ(large_file["file"].value("skip_reason", ""), "file_too_large");
}

TEST_F(RepoMapServiceTest, GitEnrichmentMarksChangedFiles) {
    create_basic_project(dir());
    run_cmd(dir(), "git init");
    run_cmd(dir(), "git config user.email test@example.com");
    run_cmd(dir(), "git config user.name Test");
    run_cmd(dir(), "git config core.autocrlf false");
    run_cmd(dir(), "git add .");
    run_cmd(dir(), "git commit -m init");
    write_text(dir() / "src/foo.cpp", "#include \"foo.hpp\"\nint changed() { return 1; }\n");

    auto ctx = make_ctx(dir());
    auto git_service = std::make_shared<ben_gear::git::GitService>(ctx);
    ben_gear::repo_map::RepoMapService service(ctx, git_service);
    auto explained = repo_map_result_json(service.explain_path("src/foo.cpp"));
    ASSERT_TRUE(explained.value("success", false));
    EXPECT_TRUE(explained["file"].value("changed", false));
    EXPECT_TRUE(array_contains_path(explained["summary"]["changed_files"], "src/foo.cpp"));
}

TEST_F(RepoMapServiceTest, ToolRegistrationMarksRepoMapToolsReadOnly) {
    auto service = std::make_shared<ben_gear::repo_map::RepoMapService>(make_ctx(dir()));
    ben_gear::llm::ToolRegistry registry;
    ben_gear::tools::register_repo_map_tools(registry, service);

    EXPECT_TRUE(registry.is_read_only("repo_map_overview"));
    EXPECT_TRUE(registry.is_read_only("repo_map_find_files"));
    EXPECT_TRUE(registry.is_read_only("repo_map_find_symbols"));
    EXPECT_TRUE(registry.is_read_only("repo_map_explain_path"));
}
