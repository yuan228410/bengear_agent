#include "intelligence/diagnostic_repair/diagnostic_repair_patch_preview_service.hpp"
#include "capabilities/tool/diagnostic_repair_tools.hpp"
#include "capabilities/tool/registry.hpp"
#include "test_framework.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string_view>
#include <utility>

using bengear::test::TmpDirTest;

class DiagnosticRepairPatchPreviewServiceTest : public TmpDirTest {};

namespace {

void write_text(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

ben_gear::workspace::WorkspaceContext make_ctx(const std::filesystem::path& root) {
    ben_gear::workspace::WorkspaceContext ctx;
    ctx.project_path = root.string();
    ctx.session_id = std::string("diagnostic-repair-patch-preview-test-session");
    ctx.tier_paths.user_dir = root / ".bengear-test-user";
    return ctx;
}

ben_gear::Json diagnostic(std::string_view path) {
    return ben_gear::Json{{"path", std::string(path)},
                          {"line", 2},
                          {"column", 10},
                          {"severity", "error"},
                          {"source", "gcc"},
                          {"message", "use of undeclared identifier nope"},
                          {"confidence", 90}};
}

ben_gear::Json diagnostic_patch_preview_result_json(
    const ben_gear::domain::AppResult<ben_gear::diagnostic_repair::RepairPatchPreviewResult>& result) {
    return result.ok() ? ben_gear::diagnostic_repair::to_json(result.value())
                       : ben_gear::Json{{"success", false},
                                        {"error_type", result.error().code},
                                        {"message", result.error().message},
                                        {"provider", "diagnostic_repair_patch_preview"},
                                        {"read_only", true}};
}

ben_gear::domain::AppResult<ben_gear::diagnostic_repair::RepairPatchPreviewResult> repair_patch_preview(
    ben_gear::diagnostic_repair::DiagnosticRepairPatchPreviewService& service,
    const ben_gear::Json& request) {
    auto parsed = ben_gear::diagnostic_repair::repair_patch_preview_request_from_json(request);
    if (!parsed.ok()) {
        return ben_gear::domain::AppResult<ben_gear::diagnostic_repair::RepairPatchPreviewResult>::failure(parsed.error());
    }
    return service.repair_patch_preview(std::move(parsed.value()));
}

} // namespace

TEST_F(DiagnosticRepairPatchPreviewServiceTest, BuildsReadOnlyPatchPreviewFromDiagnosticAndDiff) {
    write_text(dir() / "src/foo.cpp", "int main() {\n  return nope;\n}\n");
    ben_gear::diagnostic_repair::DiagnosticRepairPatchPreviewService service(make_ctx(dir()));

    auto result = diagnostic_patch_preview_result_json(repair_patch_preview(service, ben_gear::Json{
        {"diagnostics", ben_gear::Json::array({diagnostic("src/foo.cpp")})},
        {"context_lines", 1},
        {"unified_diff", "--- a/src/foo.cpp\n+++ b/src/foo.cpp\n@@ -1,3 +1,3 @@\n int main() {\n-  return nope;\n+  return 0;\n }\n"}}));

    ASSERT_TRUE(result.value("success", false));
    EXPECT_EQ(result.value("provider", ""), "diagnostic_repair_patch_preview");
    EXPECT_TRUE(result.value("read_only", false));
    EXPECT_FALSE(result["safety"].value("writes_files", true));
    EXPECT_FALSE(result["safety"].value("applies_patch", true));
    ASSERT_TRUE(result.contains("patch_preview"));
    EXPECT_TRUE(result["patch_preview"].value("can_apply", false));
    EXPECT_TRUE(result["patch_preview"]["validation"].value("hunks_match", false));
}

TEST_F(DiagnosticRepairPatchPreviewServiceTest, RequiresUnifiedDiff) {
    ben_gear::diagnostic_repair::DiagnosticRepairPatchPreviewService service(make_ctx(dir()));

    auto result = diagnostic_patch_preview_result_json(repair_patch_preview(service, ben_gear::Json{{"diagnostics", ben_gear::Json::array()}}));

    EXPECT_FALSE(result.value("success", true));
    EXPECT_EQ(result.value("error_type", ""), "invalid_arguments");
}

TEST_F(DiagnosticRepairPatchPreviewServiceTest, MarksCandidateFileMatch) {
    write_text(dir() / "src/foo.cpp", "int main() {\n  return nope;\n}\n");
    ben_gear::diagnostic_repair::DiagnosticRepairPatchPreviewService service(make_ctx(dir()));

    auto result = diagnostic_patch_preview_result_json(repair_patch_preview(service, ben_gear::Json{
        {"diagnostics", ben_gear::Json::array({diagnostic("src/foo.cpp")})},
        {"unified_diff", "--- a/src/foo.cpp\n+++ b/src/foo.cpp\n@@ -1,3 +1,3 @@\n int main() {\n-  return nope;\n+  return 0;\n }\n"}}));

    ASSERT_TRUE(result.value("success", false));
    EXPECT_TRUE(result["candidate_file_match"].value("matched", false));
    ASSERT_EQ(result["candidate_file_match"]["touched_files"].size(), 1u);
    EXPECT_EQ(result["candidate_file_match"]["touched_files"][0].get<std::string>(), "src/foo.cpp");
}

TEST_F(DiagnosticRepairPatchPreviewServiceTest, NotesCandidateFileMismatch) {
    write_text(dir() / "src/foo.cpp", "int main() {\n  return nope;\n}\n");
    write_text(dir() / "src/bar.cpp", "int value() {\n  return 1;\n}\n");
    ben_gear::diagnostic_repair::DiagnosticRepairPatchPreviewService service(make_ctx(dir()));

    auto result = diagnostic_patch_preview_result_json(repair_patch_preview(service, ben_gear::Json{
        {"diagnostics", ben_gear::Json::array({diagnostic("src/foo.cpp")})},
        {"unified_diff", "--- a/src/bar.cpp\n+++ b/src/bar.cpp\n@@ -1,3 +1,3 @@\n int value() {\n-  return 1;\n+  return 2;\n }\n"}}));

    ASSERT_TRUE(result.value("success", false));
    EXPECT_FALSE(result["candidate_file_match"].value("matched", true));
    ASSERT_GE(result["notes"].size(), 1u);
    EXPECT_THAT(result["notes"].dump(), testing::HasSubstr("does not touch"));
}

TEST_F(DiagnosticRepairPatchPreviewServiceTest, ToolRegistrationMarksPatchPreviewReadOnly) {
    auto ctx = make_ctx(dir());
    auto plan_service = std::make_shared<ben_gear::diagnostic_repair::DiagnosticRepairPlanService>(ctx);
    auto patch_preview_service = std::make_shared<ben_gear::diagnostic_repair::DiagnosticRepairPatchPreviewService>(ctx, plan_service);
    ben_gear::llm::ToolRegistry registry;
    ben_gear::tools::register_diagnostic_repair_tools(registry, plan_service, patch_preview_service);

    EXPECT_TRUE(registry.is_read_only("diagnostic_repair_plan"));
    EXPECT_TRUE(registry.is_read_only("diagnostic_repair_patch_preview"));
}
