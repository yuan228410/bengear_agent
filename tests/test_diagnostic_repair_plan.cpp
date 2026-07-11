#include "intelligence/diagnostic_repair/diagnostic_repair_plan_service.hpp"
#include "tool/diagnostic_repair_tools.hpp"
#include "tool/registry.hpp"
#include "test_framework.hpp"

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

ben_gear::Json diagnostic_repair_plan_result_json(
    const ben_gear::domain::AppResult<ben_gear::diagnostic_repair::RepairPlanResult>& result) {
    return result.ok() ? ben_gear::diagnostic_repair::to_json(result.value())
                       : ben_gear::Json{{"success", false},
                                        {"error_type", std::string(result.error().code.c_str())},
                                        {"message", std::string(result.error().message.c_str())},
                                        {"provider", "diagnostic_repair_plan"},
                                        {"read_only", true}};
}

ben_gear::domain::AppResult<ben_gear::diagnostic_repair::RepairPlanResult> repair_plan(
    ben_gear::diagnostic_repair::DiagnosticRepairPlanService& service,
    const ben_gear::Json& request) {
    auto parsed = ben_gear::diagnostic_repair::repair_plan_request_from_json(request);
    if (!parsed.ok()) {
        return ben_gear::domain::AppResult<ben_gear::diagnostic_repair::RepairPlanResult>::failure(parsed.error());
    }
    return service.repair_plan(std::move(parsed.value()));
}

} // namespace

TEST_F(DiagnosticRepairPlanServiceTest, BuildsReadOnlyPlanFromDiagnostic) {
    write_text(dir() / "src/foo.cpp", "int main() {\n  return nope;\n}\n");
    ben_gear::diagnostic_repair::DiagnosticRepairPlanService service(make_ctx(dir()));

    auto result = diagnostic_repair_plan_result_json(repair_plan(service, ben_gear::Json{{"diagnostics", ben_gear::Json::array({diagnostic("src/foo.cpp", 2, 10)})},
                                                     {"context_lines", 1}}));

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

    auto result = diagnostic_repair_plan_result_json(repair_plan(service, ben_gear::Json{{"output", "src/foo.cpp:2:10: error: missing value\n"},
                                                     {"context_lines", 0}}));

    ASSERT_TRUE(result.value("success", false));
    ASSERT_EQ(result["plans"].size(), 1u);
    EXPECT_EQ(result["plans"][0]["diagnostic"].value("path", ""), "src/foo.cpp");
    EXPECT_EQ(result["plans"][0]["diagnostic"].value("line", 0), 2);
}

TEST_F(DiagnosticRepairPlanServiceTest, ClassifiesCompileError) {
    write_text(dir() / "src/foo.cpp", "int main() { return nope; }\n");
    ben_gear::diagnostic_repair::DiagnosticRepairPlanService service(make_ctx(dir()));

    auto result = diagnostic_repair_plan_result_json(repair_plan(service, ben_gear::Json{{"diagnostics", ben_gear::Json::array({diagnostic("src/foo.cpp", 1, 21, "no member named value")})}}));

    ASSERT_TRUE(result.value("success", false));
    ASSERT_EQ(result["plans"].size(), 1u);
    EXPECT_EQ(result["plans"][0].value("issue_type", ""), "compile_error");
    EXPECT_EQ(result["summary"].value("primary_issue_type", ""), "compile_error");
}

TEST_F(DiagnosticRepairPlanServiceTest, IncludesFailureCategoryInSummaryAndPlanSteps) {
    write_text(dir() / "src/foo.cpp", "int main() { return nope; }\n");
    ben_gear::diagnostic_repair::DiagnosticRepairPlanService service(make_ctx(dir()));

    auto result = diagnostic_repair_plan_result_json(repair_plan(service, ben_gear::Json{
        {"diagnostics", ben_gear::Json::array({diagnostic("src/foo.cpp", 1, 21, "no member named value")})},
        {"failure_category", "build"},
        {"command", "ctest --output-on-failure"},
        {"timeout_seconds", 42},
        {"max_output_bytes", 12345}}));

    ASSERT_TRUE(result.value("success", false));
    EXPECT_EQ(result["summary"].value("failure_category", ""), "build");
    ASSERT_EQ(result["recommended_rerun"].size(), 4u);
    EXPECT_EQ(result["recommended_rerun"].value("command", ""), "ctest --output-on-failure");
    EXPECT_EQ(result["recommended_rerun"].value("timeout_seconds", 0), 42);
    EXPECT_EQ(result["recommended_rerun"].value("max_output_bytes", 0), 12345);
    ASSERT_EQ(result["plans"].size(), 1u);
    EXPECT_EQ(result["plans"][0].value("failure_category", ""), "build");
    ASSERT_GE(result["plans"][0]["next_steps"].size(), 1u);
    EXPECT_EQ(result["plans"][0]["next_steps"][0].value("kind", ""), "repair_build_first");
}

TEST_F(DiagnosticRepairPlanServiceTest, EnvironmentFailureCategorySuggestsEnvironmentCheckFirst) {
    ben_gear::diagnostic_repair::DiagnosticRepairPlanService service(make_ctx(dir()));

    auto result = diagnostic_repair_plan_result_json(repair_plan(service, ben_gear::Json{
        {"diagnostics", ben_gear::Json::array({diagnostic("", 0, 0, "command not found")})},
        {"failure_category", "environment"},
        {"command", "./build/bengear_tests"}}));

    ASSERT_TRUE(result.value("success", false));
    ASSERT_EQ(result["plans"].size(), 1u);
    EXPECT_EQ(result["recommended_rerun"].value("command", ""), "./build/bengear_tests");
    ASSERT_GE(result["plans"][0]["next_steps"].size(), 1u);
    EXPECT_EQ(result["plans"][0]["next_steps"][0].value("kind", ""), "inspect_environment");
}

TEST_F(DiagnosticRepairPlanServiceTest, IncludesContextNotesWhenSnippetUnavailable) {
    ben_gear::diagnostic_repair::DiagnosticRepairPlanService service(make_ctx(dir()));

    auto result = diagnostic_repair_plan_result_json(repair_plan(service, ben_gear::Json{{"diagnostics", ben_gear::Json::array({diagnostic("../outside.cpp", 1, 1)})}}));

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

    auto result = diagnostic_repair_plan_result_json(repair_plan(service, ben_gear::Json{{"diagnostics", ben_gear::Json::array({
        diagnostic("src/b.cpp", 1, 1, "warning: minor issue"),
        diagnostic("src/a.cpp", 1, 1, "error: first issue"),
        diagnostic("src/a.cpp", 2, 1, "error: second issue")})}}));

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

TEST_F(DiagnosticRepairPlanServiceTest, RuntimeAuthorizeFailureDoesNotSuggestCodeRepair) {
    ben_gear::diagnostic_repair::DiagnosticRepairPlanService service(make_ctx(dir()));
    ben_gear::Json runtime{{"execution_id", "exec-auth"},
                           {"action", "test.run"},
                           {"status", "failed"},
                           {"audit_event_id", "audit-1"},
                           {"execution", ben_gear::Json{{"status", "failed"},
                                                        {"trace", ben_gear::Json::array({
                                                            ben_gear::Json{{"kind", "validate"}, {"status", "succeeded"}},
                                                            ben_gear::Json{{"kind", "authorize"}, {"status", "failed"}, {"error_type", "permission_required"}, {"message", "approval required"}}})}}}};

    auto result = diagnostic_repair_plan_result_json(repair_plan(service, ben_gear::Json{{"runtime_execution", runtime},
                                                                                         {"failure_category", "environment"},
                                                                                         {"command", "ctest"}}));

    ASSERT_TRUE(result.value("success", false));
    EXPECT_EQ(result["summary"].value("primary_issue_type", ""), "permission_required");
    EXPECT_EQ(result["summary"].value("failed_step", ""), "authorize");
    ASSERT_EQ(result["plans"].size(), 1u);
    EXPECT_EQ(result["plans"][0].value("issue_type", ""), "permission_required");
    EXPECT_EQ(result["plans"][0]["next_steps"][0].value("kind", ""), "resolve_permission");
    EXPECT_EQ(result["plans"][0]["candidate_files"].size(), 0u);
    EXPECT_THAT(result["plans"][0].value("title", ""), testing::HasSubstr("before changing source code"));
}

TEST_F(DiagnosticRepairPlanServiceTest, RuntimeExecuteFailureKeepsDiagnosticCodeRepairAndAddsTraceEvidence) {
    write_text(dir() / "src/foo.cpp", "int main() { return nope; }\n");
    ben_gear::diagnostic_repair::DiagnosticRepairPlanService service(make_ctx(dir()));
    ben_gear::Json runtime{{"execution_id", "exec-run"},
                           {"action", "test.run"},
                           {"status", "failed"},
                           {"execution", ben_gear::Json{{"status", "failed"},
                                                        {"trace", ben_gear::Json::array({
                                                            ben_gear::Json{{"kind", "validate"}, {"status", "succeeded"}},
                                                            ben_gear::Json{{"kind", "authorize"}, {"status", "succeeded"}},
                                                            ben_gear::Json{{"kind", "checkpoint"}, {"status", "succeeded"}},
                                                            ben_gear::Json{{"kind", "execute"}, {"status", "failed"}, {"error_type", "command_failed"}}})}}}};

    auto result = diagnostic_repair_plan_result_json(repair_plan(service, ben_gear::Json{
        {"runtime_execution", runtime},
        {"diagnostics", ben_gear::Json::array({diagnostic("src/foo.cpp", 1, 21, "no member named value")})},
        {"failure_category", "build"}}));

    ASSERT_TRUE(result.value("success", false));
    EXPECT_EQ(result["summary"].value("primary_issue_type", ""), "compile_error");
    EXPECT_EQ(result["summary"].value("failed_step", ""), "execute");
    ASSERT_EQ(result["plans"].size(), 1u);
    EXPECT_EQ(result["plans"][0].value("issue_type", ""), "compile_error");
    EXPECT_EQ(result["summary"]["runtime_evidence"].value("execution_id", ""), "exec-run");
}
