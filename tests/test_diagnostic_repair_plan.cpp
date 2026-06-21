#include "ben_gear/diagnostic_repair/diagnostic_repair_plan_service.hpp"
#include "ben_gear/tools/diagnostic_repair_tools.hpp"
#include "ben_gear/tool/registry.hpp"
#include "ben_gear/test/test_framework.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string_view>

using bengear::test::TmpDirTest;

class DiagnosticRepairPlanServiceTest : public TmpDirTest {};

namespace {

void write_text(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

ben_gear::workspace::WorkspaceContext make_ctx(const std::filesystem::path& root) {
    ben_gear::workspace::WorkspaceContext ctx;
    ctx.project_path = ben_gear::base::container::String(root.string().c_str());
    ctx.session_id = ben_gear::base::container::String("diagnostic-repair-plan-test-session");
    ctx.tier_paths.user_dir = root / ".bengear-test-user";
    return ctx;
}

ben_gear::Json diagnostic(std::string_view path,
                          int line,
                          int column,
                          std::string_view message = "use of undeclared identifier nope") {
    return ben_gear::Json{{"path", std::string(path)},
                          {"line", line},
                          {"column", column},
                          {"severity", "error"},
                          {"source", "gcc"},
                          {"message", std::string(message)},
                          {"confidence", 90}};
}

} // namespace

TEST_F(DiagnosticRepairPlanServiceTest, BuildsReadOnlyPlanFromDiagnostic) {
    write_text(dir() / "src/foo.cpp", "int main() {\n  return nope;\n}\n");
    ben_gear::diagnostic_repair::DiagnosticRepairPlanService service(make_ctx(dir()));

    auto result = service.repair_plan(ben_gear::Json{{"diagnostics", ben_gear::Json::array({diagnostic("src/foo.cpp", 2, 10)})},
                                                     {"context_lines", 1}});

    ASSERT_TRUE(result.value("success", false));
    EXPECT_EQ(result.value("provider", ""), "diagnostic_repair_plan");
    EXPECT_TRUE(result.value("read_only", false));
    ASSERT_EQ(result["plans"].size(), 1u);
    const auto& plan = result["plans"][0];
    EXPECT_EQ(plan.value("rank", 0), 1);
    EXPECT_EQ(plan.value("issue_type", ""), "compile_error");
    ASSERT_TRUE(plan.contains("safety"));
    EXPECT_TRUE(plan["safety"].value("read_only", false));
    EXPECT_FALSE(plan["safety"].value("writes_files", true));
    EXPECT_FALSE(plan["safety"].value("runs_commands", true));
}

TEST_F(DiagnosticRepairPlanServiceTest, ParsesRawOutputViaContextFallback) {
    write_text(dir() / "src/foo.cpp", "int main() {\n  return missing;\n}\n");
    ben_gear::diagnostic_repair::DiagnosticRepairPlanService service(make_ctx(dir()));

    auto result = service.repair_plan(ben_gear::Json{{"output", "src/foo.cpp:2:10: error: missing value\n"},
                                                     {"context_lines", 0}});

    ASSERT_TRUE(result.value("success", false));
    ASSERT_EQ(result["plans"].size(), 1u);
    EXPECT_EQ(result["plans"][0]["diagnostic"].value("path", ""), "src/foo.cpp");
    EXPECT_EQ(result["plans"][0]["diagnostic"].value("line", 0), 2);
}

TEST_F(DiagnosticRepairPlanServiceTest, ClassifiesCompileError) {
    write_text(dir() / "src/foo.cpp", "int main() { return nope; }\n");
    ben_gear::diagnostic_repair::DiagnosticRepairPlanService service(make_ctx(dir()));

    auto result = service.repair_plan(ben_gear::Json{{"diagnostics", ben_gear::Json::array({diagnostic("src/foo.cpp", 1, 21, "no member named value")})}});

    ASSERT_TRUE(result.value("success", false));
    ASSERT_EQ(result["plans"].size(), 1u);
    EXPECT_EQ(result["plans"][0].value("issue_type", ""), "compile_error");
    EXPECT_EQ(result["summary"].value("primary_issue_type", ""), "compile_error");
}

TEST_F(DiagnosticRepairPlanServiceTest, IncludesContextNotesWhenSnippetUnavailable) {
    ben_gear::diagnostic_repair::DiagnosticRepairPlanService service(make_ctx(dir()));

    auto result = service.repair_plan(ben_gear::Json{{"diagnostics", ben_gear::Json::array({diagnostic("../outside.cpp", 1, 1)})}});

    ASSERT_TRUE(result.value("success", false));
    ASSERT_EQ(result["plans"].size(), 1u);
    ASSERT_GE(result["plans"][0]["notes"].size(), 1u);
    ASSERT_GE(result["plans"][0]["evidence"].size(), 1u);
    EXPECT_THAT(result["plans"][0]["evidence"].dump().to_std_string(), testing::HasSubstr("Context note"));
}

TEST_F(DiagnosticRepairPlanServiceTest, RanksRepeatedFileDiagnosticsHigher) {
    write_text(dir() / "src/a.cpp", "one\ntwo\nthree\n");
    write_text(dir() / "src/b.cpp", "one\ntwo\nthree\n");
    ben_gear::diagnostic_repair::DiagnosticRepairPlanService service(make_ctx(dir()));

    auto result = service.repair_plan(ben_gear::Json{{"diagnostics", ben_gear::Json::array({
        diagnostic("src/b.cpp", 1, 1, "warning: minor issue"),
        diagnostic("src/a.cpp", 1, 1, "error: first issue"),
        diagnostic("src/a.cpp", 2, 1, "error: second issue")})}});

    ASSERT_TRUE(result.value("success", false));
    ASSERT_GE(result["plans"].size(), 2u);
    EXPECT_EQ(result["plans"][0]["diagnostic"].value("path", ""), "src/a.cpp");
    ASSERT_GE(result["plans"][0]["candidate_files"].size(), 1u);
    EXPECT_EQ(result["plans"][0]["candidate_files"][0].value("diagnostic_count", 0), 2);
}

TEST_F(DiagnosticRepairPlanServiceTest, ToolRegistrationMarksRepairPlanReadOnly) {
    auto service = std::make_shared<ben_gear::diagnostic_repair::DiagnosticRepairPlanService>(make_ctx(dir()));
    ben_gear::llm::ToolRegistry registry;
    ben_gear::tools::register_diagnostic_repair_tools(registry, service);

    EXPECT_TRUE(registry.is_read_only("diagnostic_repair_plan"));
    EXPECT_FALSE(registry.is_read_only("diagnostic_repair_patch_preview"));
}
