#include "ben_gear/diagnostic_repair/diagnostic_repair_patch_draft_service.hpp"
#include "ben_gear/patch/patch_service.hpp"

#include "ben_gear/test/test_framework.hpp"

#include <filesystem>
#include <fstream>

namespace ben_gear::diagnostic_repair {
namespace {

workspace::WorkspaceContext ctx_for(const std::filesystem::path& root) {
    workspace::WorkspaceContext ctx;
    ctx.project_path = base::container::String(root.string().c_str());
    ctx.tier_paths.workspace_dir = root;
    ctx.tier_paths.user_dir = root / ".user";
    return ctx;
}

void write_file(const std::filesystem::path& path, const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out << content;
}

std::vector<std::string> read_text_lines(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) lines.push_back(line);
    return lines;
}

TEST(DiagnosticRepairPatchDraftServiceTest, ReturnsNoDraftWhenContextInsufficient) {
    auto root = std::filesystem::temp_directory_path() / "bengear_patch_draft_no_context";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);
    write_file(root / "CMakeLists.txt", "add_library(app STATIC\n    src/a.cpp\n)\n");

    auto parsed = repair_patch_draft_request_from_json(Json{{"output", "ordinary failure"}, {"diagnostics", Json::array()}});
    ASSERT_TRUE(parsed.ok());
    DiagnosticRepairPatchDraftService service(ctx_for(root));
    auto result = service.repair_patch_draft(std::move(parsed.value()));
    ASSERT_TRUE(result.ok());
    EXPECT_FALSE(result.value().drafted);
    EXPECT_EQ(result.value().status, "no_draft");
    std::filesystem::remove_all(root);
}

TEST(DiagnosticRepairPatchDraftServiceTest, GeneratesCMakeMissingSourceDraft) {
    auto root = std::filesystem::temp_directory_path() / "bengear_patch_draft_cmake";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "src");
    write_file(root / "CMakeLists.txt", "add_library(app STATIC\n    src/a.cpp\n)\n");
    write_file(root / "src/a.cpp", "int a() { return 1; }\n");

    Json request{{"output", "CMake Error: Cannot find source file: src/b.cpp"},
                 {"diagnostics", Json::array()},
                 {"cmake_target", "app"},
                 {"code_context", Json{{"context_pack_id", "ctx-1"}, {"primary_files", Json::array({"CMakeLists.txt"})}}}};
    auto parsed = repair_patch_draft_request_from_json(request);
    ASSERT_TRUE(parsed.ok());
    DiagnosticRepairPatchDraftService service(ctx_for(root));
    auto result = service.repair_patch_draft(std::move(parsed.value()));
    ASSERT_TRUE(result.ok());
    EXPECT_TRUE(result.value().drafted);
    EXPECT_EQ(result.value().status, "previewed");
    EXPECT_NE(result.value().unified_diff.find("+    src/b.cpp"), std::string::npos);
    EXPECT_EQ(result.value().draft_rule, "deterministic_cmake_missing_source");
    EXPECT_EQ(result.value().context_pack_id, "ctx-1");
    std::filesystem::remove_all(root);
}


TEST(DiagnosticRepairPatchDraftServiceTest, GoldenFixtureDraftPreviewAndApplySucceeds) {
    auto root = std::filesystem::temp_directory_path() / "bengear_patch_draft_golden";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "src");
    write_file(root / "CMakeLists.txt", "cmake_minimum_required(VERSION 3.20)\nproject(golden LANGUAGES CXX)\nadd_library(app STATIC\n    src/a.cpp\n)\n");
    write_file(root / "src/a.cpp", "int a() { return 1; }\n");
    write_file(root / "src/b.cpp", "int b() { return 2; }\n");

    auto ctx = ctx_for(root);
    Json request{{"output", "CMake Error: Cannot find source file: src/b.cpp"},
                 {"diagnostics", Json::array()},
                 {"cmake_target", "app"},
                 {"code_context", Json{{"context_pack_id", "golden-context"}, {"primary_files", Json::array({"CMakeLists.txt", "src/b.cpp"})}}}};
    auto parsed = repair_patch_draft_request_from_json(request);
    ASSERT_TRUE(parsed.ok());
    DiagnosticRepairPatchDraftService service(ctx);
    auto draft = service.repair_patch_draft(std::move(parsed.value()));
    ASSERT_TRUE(draft.ok());
    ASSERT_TRUE(draft.value().drafted);
    ASSERT_TRUE(draft.value().preview.value("success", false));
    EXPECT_TRUE(draft.value().preview.value("patch_preview", Json::object()).value("can_apply", false));

    patch::PatchService patch_service(ctx);
    auto applied = patch_service.apply(draft.value().unified_diff, "golden repair draft");
    ASSERT_TRUE(applied.ok());
    auto cmake = read_text_lines(root / "CMakeLists.txt");
    EXPECT_NE(std::find(cmake.begin(), cmake.end(), "    src/b.cpp"), cmake.end());
    std::filesystem::remove_all(root);
}


} // namespace
} // namespace ben_gear::diagnostic_repair
