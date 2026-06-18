#include "ben_gear/test_loop/test_loop_service.hpp"
#include "ben_gear/test/test_framework.hpp"

#include <filesystem>
#include <fstream>

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
    EXPECT_TRUE(inspected.value("success", false));
    ASSERT_GE(inspected["suggestions"].size(), 1u);
    bool found = false;
    for (const auto& suggestion : inspected["suggestions"]) {
        if (suggestion.value("id", "") == "cmake-test") found = true;
    }
    EXPECT_TRUE(found);
}

TEST_F(TestLoopServiceTest, RunReturnsSuccessfulResult) {
    ben_gear::test_loop::TestLoopService service(make_ctx(dir()));
    auto result = service.run("printf 'ok\\n'", ".", 5);
    EXPECT_TRUE(result.value("success", false));
    EXPECT_EQ(result.value("exit_code", -1), 0);
    EXPECT_NE(result.value("output", "").find("ok"), std::string::npos);
}

TEST_F(TestLoopServiceTest, RunSummarizesFailureLines) {
    ben_gear::test_loop::TestLoopService service(make_ctx(dir()));
    auto result = service.run("printf 'test failed: expected 1 actual 2\\n' && exit 2", ".", 5);
    EXPECT_FALSE(result.value("success", true));
    EXPECT_EQ(result.value("exit_code", 0), 2);
    ASSERT_GE(result["failure_summary"].size(), 1u);
    EXPECT_NE(result["failure_summary"][0].get<std::string>().find("failed"), std::string::npos);
}

TEST_F(TestLoopServiceTest, RunRejectsCwdOutsideWorkspace) {
    ben_gear::test_loop::TestLoopService service(make_ctx(dir()));
    auto result = service.run("true", "..", 5);
    EXPECT_FALSE(result.value("success", true));
    EXPECT_EQ(result.value("error_type", ""), "path_outside_workspace");
}

TEST_F(TestLoopServiceTest, RunTimesOut) {
    ben_gear::test_loop::TestLoopService service(make_ctx(dir()));
    auto result = service.run("sleep 2", ".", 1);
    EXPECT_FALSE(result.value("success", true));
    EXPECT_TRUE(result.value("timed_out", false));
}
