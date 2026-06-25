#include "ben_gear/test_loop/test_loop_service.hpp"
#include "ben_gear/test_loop/diagnostics.hpp"
#include "ben_gear/test/test_framework.hpp"

#include <filesystem>
#include <fstream>
#include <string_view>

using bengear::test::TmpDirTest;

class TestLoopServiceTest : public TmpDirTest {};

namespace {

void write_text(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

ben_gear::workspace::WorkspaceContext make_ctx(const std::filesystem::path& root) {
    ben_gear::workspace::WorkspaceContext ctx;
    ctx.project_path = ben_gear::base::container::String(root.string().c_str());
    return ctx;
}

} // namespace

TEST_F(TestLoopServiceTest, InspectDetectsCMakeProject) {
    write_text(dir() / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\n");
    ben_gear::test_loop::TestLoopService service(make_ctx(dir()));
    auto inspected = service.inspect();
    ASSERT_TRUE(inspected.ok());
    auto inspected_json = ben_gear::test_loop::to_json(inspected.value());
    EXPECT_TRUE(inspected_json.value("success", false));
    ASSERT_GE(inspected_json["suggestions"].size(), 1u);
    bool found = false;
    for (const auto& suggestion : inspected_json["suggestions"]) {
        if (suggestion.value("id", "") == "cmake-test") found = true;
    }
    EXPECT_TRUE(found);
}

TEST_F(TestLoopServiceTest, RunReturnsSuccessfulResult) {
    ben_gear::test_loop::TestLoopService service(make_ctx(dir()));
    auto result = service.run("printf 'ok\n'", ".", 5);
    ASSERT_TRUE(result.ok());
    EXPECT_TRUE(result.value().success);
    EXPECT_EQ(result.value().exit_code, 0);
    EXPECT_NE(result.value().output.find("ok"), std::string::npos);
}

TEST_F(TestLoopServiceTest, RunSummarizesFailureLines) {
    ben_gear::test_loop::TestLoopService service(make_ctx(dir()));
    auto result = service.run("printf 'test failed: expected 1 actual 2\n' && exit 2", ".", 5);
    ASSERT_TRUE(result.ok());
    EXPECT_FALSE(result.value().success);
    EXPECT_EQ(result.value().exit_code, 2);
    ASSERT_GE(result.value().failure_summary.size(), 1u);
    EXPECT_NE(result.value().failure_summary[0].find("failed"), std::string::npos);
}

TEST_F(TestLoopServiceTest, RunClassifiesTestFailure) {
    ben_gear::test_loop::TestLoopService service(make_ctx(dir()));
    auto result = service.run("printf 'test failed: expected 1 actual 2\n' && exit 2", ".", 5);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().failure_category, "test");
    auto json = ben_gear::test_loop::to_json(result.value());
    EXPECT_EQ(json.value("failure_category", ""), "test");
}

TEST_F(TestLoopServiceTest, RunClassifiesBuildFailure) {
    ben_gear::test_loop::TestLoopService service(make_ctx(dir()));
    auto result = service.run("printf 'src/foo.cpp:1:1: error: bad thing\n' && exit 2", ".", 5);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().failure_category, "build");
}

TEST_F(TestLoopServiceTest, RunClassifiesEnvironmentFailure) {
    ben_gear::test_loop::TestLoopService service(make_ctx(dir()));
    auto result = service.run("printf 'command not found: missing-tool\n' && exit 127", ".", 5);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result.value().failure_category, "environment");
}

TEST_F(TestLoopServiceTest, RunParsesGccDiagnostic) {
    write_text(dir() / "src/foo.cpp", "int main() { return 0; }\n");
    ben_gear::test_loop::TestLoopService service(make_ctx(dir()));
    auto result = service.run("printf 'src/foo.cpp:12:5: error: bad thing\n' && exit 2", ".", 5);
    ASSERT_TRUE(result.ok());
    ASSERT_GE(result.value().diagnostics.size(), 1u);
    const auto& diagnostic = result.value().diagnostics[0];
    EXPECT_EQ(diagnostic.path, "src/foo.cpp");
    EXPECT_EQ(diagnostic.line, 12);
    EXPECT_EQ(diagnostic.column, 5);
    EXPECT_EQ(diagnostic.severity, "error");
    EXPECT_EQ(diagnostic.source, "gcc");
    EXPECT_NE(diagnostic.message.find("bad thing"), std::string::npos);
}

TEST_F(TestLoopServiceTest, RunParsesMsvcDiagnostic) {
    write_text(dir() / "src/foo.cpp", "int main() { return 0; }\n");
    ben_gear::test_loop::TestLoopService service(make_ctx(dir()));
    auto result = service.run("printf '%s\n' 'src\\foo.cpp(12,5): error C2143: syntax error' && exit 2", ".", 5);
    ASSERT_TRUE(result.ok());
    ASSERT_GE(result.value().diagnostics.size(), 1u);
    const auto& diagnostic = result.value().diagnostics[0];
    EXPECT_EQ(diagnostic.path, "src/foo.cpp");
    EXPECT_EQ(diagnostic.line, 12);
    EXPECT_EQ(diagnostic.column, 5);
    EXPECT_EQ(diagnostic.severity, "error");
    EXPECT_EQ(diagnostic.source, "msvc");
    EXPECT_EQ(diagnostic.code, "C2143");
}

TEST_F(TestLoopServiceTest, RunParsesGtestFailure) {
    write_text(dir() / "tests/test_foo.cpp", "TEST(FooTest, DoesThing) {}\n");
    ben_gear::test_loop::TestLoopService service(make_ctx(dir()));
    auto result = service.run("printf 'tests/test_foo.cpp:27: Failure\n[  FAILED  ] FooTest.DoesThing\n' && exit 1", ".", 5);
    ASSERT_TRUE(result.ok());
    ASSERT_GE(result.value().diagnostics.size(), 2u);
    EXPECT_EQ(result.value().diagnostics[0].path, "tests/test_foo.cpp");
    EXPECT_EQ(result.value().diagnostics[0].severity, "failure");
    EXPECT_EQ(result.value().diagnostics[1].test_name, "FooTest.DoesThing");
}

TEST_F(TestLoopServiceTest, ParseDiagnosticsParsesPytestTraceback) {
    write_text(dir() / "tests/test_foo.py", "def test_bar(): pass\n");
    ben_gear::test_loop::DiagnosticParseOptions options{dir(), dir(), 100};
    auto parsed = ben_gear::test_loop::parse_diagnostics("File \"tests/test_foo.py\", line 9, in test_bar\nE   AssertionError: expected 1\n", options);
    ASSERT_GE(parsed.diagnostics.size(), 2u);
    EXPECT_EQ(parsed.diagnostics[0].path, "tests/test_foo.py");
    EXPECT_EQ(parsed.diagnostics[0].line, 9);
    EXPECT_EQ(parsed.diagnostics[0].source, "pytest");
    EXPECT_EQ(parsed.diagnostics[0].test_name, "test_bar");
}

TEST_F(TestLoopServiceTest, RunKeepsFailureSummaryWithDiagnostics) {
    write_text(dir() / "src/foo.cpp", "int main() { return 0; }\n");
    ben_gear::test_loop::TestLoopService service(make_ctx(dir()));
    auto result = service.run("printf 'src/foo.cpp:1:1: error: expected value\n' && exit 2", ".", 5);
    ASSERT_TRUE(result.ok());
    EXPECT_FALSE(result.value().success);
    ASSERT_GE(result.value().failure_summary.size(), 1u);
    ASSERT_GE(result.value().diagnostics.size(), 1u);
}

TEST_F(TestLoopServiceTest, ParseDiagnosticsOmitsOutsideWorkspaceDiagnosticPath) {
    ben_gear::test_loop::DiagnosticParseOptions options{dir(), dir(), 100};
    auto parsed = ben_gear::test_loop::parse_diagnostics("/tmp/outside.cpp:1:1: error: nope\n", options);
    EXPECT_EQ(parsed.diagnostics.size(), 0u);
}

TEST_F(TestLoopServiceTest, ParseDiagnosticsMarksTruncated) {
    write_text(dir() / "src/foo.cpp", "int main() { return 0; }\n");
    ben_gear::test_loop::DiagnosticParseOptions options{dir(), dir(), 1};
    auto parsed = ben_gear::test_loop::parse_diagnostics("src/foo.cpp:1:1: error: one\nsrc/foo.cpp:2:1: error: two\n", options);
    EXPECT_EQ(parsed.diagnostics.size(), 1u);
    EXPECT_TRUE(parsed.truncated);
}

TEST_F(TestLoopServiceTest, RunRejectsCwdOutsideWorkspace) {
    ben_gear::test_loop::TestLoopService service(make_ctx(dir()));
    auto result = service.run("true", "..", 5);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(std::string(result.error().code.c_str()), "path_outside_workspace");
}

TEST_F(TestLoopServiceTest, RunTimesOut) {
    ben_gear::test_loop::TestLoopService service(make_ctx(dir()));
    auto result = service.run("sleep 2", ".", 1);
    ASSERT_TRUE(result.ok());
    EXPECT_FALSE(result.value().success);
    EXPECT_TRUE(result.value().timed_out);
    EXPECT_EQ(result.value().failure_category, "timeout");
}
