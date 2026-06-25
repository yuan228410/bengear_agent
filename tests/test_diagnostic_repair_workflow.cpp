#include "ben_gear/diagnostic_repair/diagnostic_repair_workflow_service.hpp"
#include "ben_gear/tools/diagnostic_repair_tools.hpp"
#include "ben_gear/tool/registry.hpp"
#include "ben_gear/test/test_framework.hpp"

#include <filesystem>
#include <fstream>
#include <string_view>

using bengear::test::TmpDirTest;

class DiagnosticRepairWorkflowServiceTest : public TmpDirTest {};

namespace {

void write_text(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

ben_gear::application::WorkspaceResolver make_resolver(const std::filesystem::path& root) {
    ben_gear::application::WorkspaceResolverConfig config;
    config.data_root = root / ".bengear-data";
    config.fallback_project_path = ben_gear::base::container::String(root.string().c_str());
    return ben_gear::application::WorkspaceResolver(config);
}

ben_gear::Json diagnostic(std::string_view path, std::string_view message = "use of undeclared identifier nope") {
    return ben_gear::Json{{"path", std::string(path)},
                          {"line", 2},
                          {"column", 10},
                          {"severity", "error"},
                          {"source", "gcc"},
                          {"message", std::string(message)},
                          {"confidence", 90}};
}

ben_gear::Json workflow_result_json(
    const ben_gear::domain::AppResult<ben_gear::diagnostic_repair::RepairWorkflowResult>& result) {
    return result.ok() ? ben_gear::diagnostic_repair::to_json(result.value())
                       : ben_gear::Json{{"success", false},
                                        {"error_type", std::string(result.error().code.c_str())},
                                        {"message", std::string(result.error().message.c_str())},
                                        {"provider", "diagnostic_repair_workflow"}};
}

ben_gear::domain::AppResult<ben_gear::diagnostic_repair::RepairWorkflowResult> repair_workflow(
    ben_gear::diagnostic_repair::DiagnosticRepairWorkflowService& service,
    const ben_gear::Json& request) {
    auto parsed = ben_gear::diagnostic_repair::repair_workflow_request_from_json(request);
    if (!parsed.ok()) return ben_gear::domain::AppResult<ben_gear::diagnostic_repair::RepairWorkflowResult>::failure(parsed.error());
    return service.repair_workflow(parsed.value());
}

} // namespace

TEST_F(DiagnosticRepairWorkflowServiceTest, AppliesSafeCandidateAndRerunsRecommendedCommand) {
    write_text(dir() / "src/foo.cpp", "int main() {\n  return nope;\n}\n");
    auto resolver = make_resolver(dir());
    ben_gear::diagnostic_repair::DiagnosticRepairWorkflowService service(resolver);

    auto result = workflow_result_json(repair_workflow(service, ben_gear::Json{
        {"username", "test-user"},
        {"workspace", "default"},
        {"session_id", "repair-workflow-test-session"},
        {"diagnostics", ben_gear::Json::array({diagnostic("src/foo.cpp")})},
        {"failure_category", "build"},
        {"command", "grep -q 'return 0' src/foo.cpp"},
        {"timeout_seconds", 5},
        {"patch_candidates", ben_gear::Json::array({ben_gear::Json{
            {"id", "fix-return"},
            {"description", "replace undeclared identifier with zero"},
            {"unified_diff", "--- a/src/foo.cpp\n+++ b/src/foo.cpp\n@@ -1,3 +1,3 @@\n int main() {\n-  return nope;\n+  return 0;\n }\n"}}})}}));

    ASSERT_TRUE(result.value("success", false));
    EXPECT_EQ(result.value("provider", ""), "diagnostic_repair_workflow");
    EXPECT_EQ(result.value("status", ""), "repaired");
    ASSERT_EQ(result["attempts"].size(), 1u);
    EXPECT_EQ(result["attempts"][0].value("status", ""), "repaired");
    EXPECT_TRUE(result["attempts"][0]["test_result"].value("success", false));
    EXPECT_TRUE(result["safety"].value("uses_command_governance", false));
}

TEST_F(DiagnosticRepairWorkflowServiceTest, ReturnsPlanWhenNoPatchCandidatesProvided) {
    write_text(dir() / "src/foo.cpp", "int main() {\n  return nope;\n}\n");
    auto resolver = make_resolver(dir());
    ben_gear::diagnostic_repair::DiagnosticRepairWorkflowService service(resolver);

    auto result = workflow_result_json(repair_workflow(service, ben_gear::Json{
        {"diagnostics", ben_gear::Json::array({diagnostic("src/foo.cpp")})},
        {"command", "grep -q 'return 0' src/foo.cpp"}}));

    EXPECT_FALSE(result.value("success", true));
    EXPECT_EQ(result.value("status", ""), "no_patch_candidates");
    EXPECT_TRUE(result["summary"].value("needs_patch_candidate", false));
    EXPECT_TRUE(result.contains("repair_plan"));
}

TEST_F(DiagnosticRepairWorkflowServiceTest, StopsAtMaxIterationsAndReportsNotRepaired) {
    write_text(dir() / "src/foo.cpp", "int main() {\n  return nope;\n}\n");
    write_text(dir() / "src/bar.cpp", "int value() {\n  return 1;\n}\n");
    auto resolver = make_resolver(dir());
    ben_gear::diagnostic_repair::DiagnosticRepairWorkflowService service(resolver);

    auto result = workflow_result_json(repair_workflow(service, ben_gear::Json{
        {"diagnostics", ben_gear::Json::array({diagnostic("src/foo.cpp")})},
        {"command", "grep -q 'return 0' src/foo.cpp"},
        {"max_iterations", 1},
        {"patch_candidates", ben_gear::Json::array({ben_gear::Json{
            {"id", "wrong-file"},
            {"unified_diff", "--- a/src/bar.cpp\n+++ b/src/bar.cpp\n@@ -1,3 +1,3 @@\n int value() {\n-  return 1;\n+  return 2;\n }\n"}}})}}));

    EXPECT_FALSE(result.value("success", true));
    EXPECT_EQ(result.value("status", ""), "not_repaired");
    EXPECT_EQ(result.value("iterations", 0), 1);
    ASSERT_EQ(result["attempts"].size(), 1u);
}

TEST_F(DiagnosticRepairWorkflowServiceTest, ToolRegistrationAddsWorkflowAsMutatingTool) {
    auto resolver = make_resolver(dir());
    auto workflow_service = std::make_shared<ben_gear::diagnostic_repair::DiagnosticRepairWorkflowService>(resolver);
    ben_gear::llm::ToolRegistry registry;
    ben_gear::tools::register_diagnostic_repair_tools(registry, nullptr, nullptr, workflow_service);

    EXPECT_FALSE(registry.is_read_only("diagnostic_repair_workflow"));
}
