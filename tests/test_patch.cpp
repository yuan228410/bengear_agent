#include "capabilities/patch/patch_service.hpp"
#include "capabilities/patch/diff_parser.hpp"
#include "test_framework.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using bengear::test::TmpDirTest;

class PatchServiceTest : public TmpDirTest {};

namespace {

ben_gear::Json patch_result_json(const ben_gear::domain::AppResult<ben_gear::patch::PatchValidatedPreviewResult>& result) {
    return result.ok() ? ben_gear::patch::to_json(result.value())
                       : ben_gear::Json{{"success", false},
                                        {"error_type", result.error().code},
                                        {"message", result.error().message}};
}

ben_gear::Json patch_result_json(const ben_gear::domain::AppResult<ben_gear::patch::PatchApplyResult>& result) {
    return result.ok() ? ben_gear::patch::to_json(result.value())
                       : ben_gear::Json{{"success", false},
                                        {"error_type", result.error().code},
                                        {"message", result.error().message}};
}

ben_gear::Json patch_result_json(const ben_gear::domain::AppResult<ben_gear::patch::PatchListChangesResult>& result) {
    return result.ok() ? ben_gear::patch::to_json(result.value())
                       : ben_gear::Json{{"success", false},
                                        {"error_type", result.error().code},
                                        {"message", result.error().message}};
}

ben_gear::Json patch_result_json(const ben_gear::domain::AppResult<ben_gear::patch::PatchReadChangeResult>& result) {
    return result.ok() ? ben_gear::patch::to_json(result.value())
                       : ben_gear::Json{{"success", false},
                                        {"error_type", result.error().code},
                                        {"message", result.error().message}};
}

ben_gear::Json patch_result_json(const ben_gear::domain::AppResult<ben_gear::patch::PatchRevertResult>& result) {
    return result.ok() ? ben_gear::patch::to_json(result.value())
                       : ben_gear::Json{{"success", false},
                                        {"error_type", result.error().code},
                                        {"message", result.error().message}};
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void write_text(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

ben_gear::workspace::WorkspaceContext make_ctx(const std::filesystem::path& root) {
    ben_gear::workspace::WorkspaceContext ctx;
    ctx.project_path = root.string();
    ctx.session_id = std::string("patch-test-session");
    ctx.tier_paths.user_dir = root / ".bengear-test-user";
    return ctx;
}

} // namespace

TEST_F(PatchServiceTest, PreviewSimpleModify) {
    ben_gear::patch::PatchService service(make_ctx(dir()));
    auto preview = service.preview("--- a/file.txt\n+++ b/file.txt\n@@ -1,2 +1,2 @@\n hello\n-old\n+new\n");
    EXPECT_TRUE(preview.success);
    EXPECT_TRUE(preview.can_apply);
    ASSERT_EQ(preview.files.size(), 1u);
    EXPECT_EQ(preview.additions, 1);
    EXPECT_EQ(preview.deletions, 1);
}

TEST_F(PatchServiceTest, ParseGitDiffFormat) {
    auto preview = ben_gear::patch::parse_unified_diff(
        "diff --git a/file.txt b/file.txt\n"
        "index e69de29..7898192 100644\n"
        "--- a/file.txt\n"
        "+++ b/file.txt\n"
        "@@ -0,0 +1,2 @@\n"
        "+hello\n"
        "+world\n");
    EXPECT_TRUE(preview.success);
    EXPECT_TRUE(preview.can_apply);
    ASSERT_EQ(preview.files.size(), 1u);
    EXPECT_EQ(preview.files[0].new_path, std::filesystem::path("file.txt"));
    EXPECT_EQ(preview.additions, 2);
    EXPECT_EQ(preview.deletions, 0);
}

TEST_F(PatchServiceTest, EmptyPatchPreviewIsReadOnlySuccess) {
    auto preview = ben_gear::patch::empty_patch_preview();
    EXPECT_TRUE(preview.success);
    EXPECT_FALSE(preview.can_apply);
    EXPECT_TRUE(preview.files.empty());
}

TEST_F(PatchServiceTest, ApplyAndRevertSimpleModify) {
    auto file = dir() / "file.txt";
    write_text(file, "hello\nold");
    ben_gear::patch::PatchService service(make_ctx(dir()));

    auto applied = patch_result_json(service.apply("--- a/file.txt\n+++ b/file.txt\n@@ -1,2 +1,2 @@\n hello\n-old\n+new\n", "test change"));
    EXPECT_TRUE(applied.value("success", false));
    EXPECT_EQ(read_text(file), "hello\nnew");

    auto reverted = patch_result_json(service.revert(applied.value("change_id", "")));
    EXPECT_TRUE(reverted.value("success", false));
    EXPECT_EQ(read_text(file), "hello\nold");
}

TEST_F(PatchServiceTest, ListAndReadChanges) {
    auto file = dir() / "file.txt";
    write_text(file, "hello\nold");
    ben_gear::patch::PatchService service(make_ctx(dir()));

    auto applied = patch_result_json(service.apply("--- a/file.txt\n+++ b/file.txt\n@@ -1,2 +1,2 @@\n hello\n-old\n+new\n", "inspectable change"));
    ASSERT_TRUE(applied.value("success", false));
    auto changes = patch_result_json(service.list_changes());
    EXPECT_TRUE(changes.value("success", false));
    ASSERT_EQ(changes["changes"].size(), 1u);
    EXPECT_EQ(changes["changes"][0].value("description", ""), "inspectable change");

    auto change = patch_result_json(service.read_change(applied.value("change_id", "")));
    EXPECT_TRUE(change.value("success", false));
    EXPECT_EQ(change["change"].value("description", ""), "inspectable change");
}

TEST_F(PatchServiceTest, ApplyStoresStructuredPatchForReview) {
    auto file = dir() / "file.txt";
    write_text(file, "hello\nold");
    ben_gear::patch::PatchService service(make_ctx(dir()));

    auto applied = patch_result_json(service.apply("--- a/file.txt\n+++ b/file.txt\n@@ -1,2 +1,2 @@\n hello\n-old\n+new\n", "reviewable change"));
    ASSERT_TRUE(applied.value("success", false));

    auto change = patch_result_json(service.read_change(applied.value("change_id", "")));
    ASSERT_TRUE(change.value("success", false));
    auto patch = change["change"]["patch"];
    EXPECT_TRUE(patch.value("success", false));
    ASSERT_EQ(patch["files"].size(), 1u);
    ASSERT_EQ(patch["files"][0]["hunks"].size(), 1u);
    auto lines = patch["files"][0]["hunks"][0]["lines"];
    ASSERT_EQ(lines.size(), 3u);
    EXPECT_EQ(lines[0].value("kind", ""), "context");
    EXPECT_EQ(lines[1].value("kind", ""), "remove");
    EXPECT_EQ(lines[1].value("text", ""), "old");
    EXPECT_EQ(lines[2].value("kind", ""), "add");
    EXPECT_EQ(lines[2].value("text", ""), "new");
}

TEST_F(PatchServiceTest, ReadChangeDoesNotRequireCurrentFileForDiff) {
    auto file = dir() / "file.txt";
    write_text(file, "hello\nold");
    ben_gear::patch::PatchService service(make_ctx(dir()));

    auto applied = patch_result_json(service.apply("--- a/file.txt\n+++ b/file.txt\n@@ -1,2 +1,2 @@\n hello\n-old\n+new\n", "stable review data"));
    ASSERT_TRUE(applied.value("success", false));
    write_text(file, "unrelated\nmutation");

    auto change = patch_result_json(service.read_change(applied.value("change_id", "")));
    ASSERT_TRUE(change.value("success", false));
    auto patch = change["change"]["patch"];
    ASSERT_EQ(patch["files"].size(), 1u);
    EXPECT_EQ(patch["files"][0].value("new_path", ""), "file.txt");
    EXPECT_EQ(patch["summary"].value("additions", 0), 1);
    EXPECT_EQ(patch["summary"].value("deletions", 0), 1);
}

TEST_F(PatchServiceTest, ChangeStoreLoadsLegacyRecordWithoutPatch) {
    auto ctx = make_ctx(dir());
    auto changes_dir = ctx.tier_paths.user_dir / "changes" / "patch-test-session";
    std::filesystem::create_directories(changes_dir);
    write_text(changes_dir / "chg_legacy.json",
               R"({"change_id":"chg_legacy","session_id":"patch-test-session","description":"legacy","created_at":"2026-01-01T00:00:00Z","files":[{"path":"file.txt","kind":"modify","existed_before":true,"exists_after":true,"before_hash":"a","after_hash":"b","before_content":"old"}],"reverted":false,"reverted_at":""})");

    ben_gear::patch::PatchService service(ctx);
    auto change = patch_result_json(service.read_change("chg_legacy"));
    ASSERT_TRUE(change.value("success", false));
    EXPECT_EQ(change["change"].value("description", ""), "legacy");
    EXPECT_TRUE(change["change"].contains("patch"));
    EXPECT_FALSE(change["change"]["patch"].value("success", true));
    EXPECT_EQ(change["change"]["patch"]["files"].size(), 0u);
}

TEST_F(PatchServiceTest, ConflictDoesNotModifyFile) {
    auto file = dir() / "file.txt";
    write_text(file, "hello\nactual");
    ben_gear::patch::PatchService service(make_ctx(dir()));

    auto applied = patch_result_json(service.apply("--- a/file.txt\n+++ b/file.txt\n@@ -1,2 +1,2 @@\n hello\n-old\n+new\n", "conflict"));
    EXPECT_FALSE(applied.value("success", true));
    EXPECT_EQ(applied.value("error_type", ""), "patch_conflict");
    EXPECT_EQ(read_text(file), "hello\nactual");
}

TEST_F(PatchServiceTest, RejectPathTraversal) {
    ben_gear::patch::PatchService service(make_ctx(dir()));
    auto applied = patch_result_json(service.apply("--- /dev/null\n+++ b/../outside.txt\n@@ -0,0 +1 @@\n+bad\n", "escape"));
    EXPECT_FALSE(applied.value("success", true));
    EXPECT_EQ(applied.value("error_type", ""), "path_outside_workspace");
}

TEST_F(PatchServiceTest, PreviewValidatedAcceptsMatchingPatch) {
    auto file = dir() / "file.txt";
    write_text(file, "hello\nold");
    ben_gear::patch::PatchService service(make_ctx(dir()));

    auto preview = patch_result_json(service.preview_validated("--- a/file.txt\n+++ b/file.txt\n@@ -1,2 +1,2 @@\n hello\n-old\n+new\n"));

    EXPECT_TRUE(preview.value("success", false));
    EXPECT_TRUE(preview.value("can_apply", false));
    EXPECT_TRUE(preview["validation"].value("checked_workspace", false));
    EXPECT_TRUE(preview["validation"].value("paths_inside_workspace", false));
    EXPECT_TRUE(preview["validation"].value("hunks_match", false));
    EXPECT_FALSE(preview["validation"].value("writes_files", true));
}

TEST_F(PatchServiceTest, PreviewValidatedRejectsWorkspaceEscape) {
    ben_gear::patch::PatchService service(make_ctx(dir()));

    auto preview = patch_result_json(service.preview_validated("--- /dev/null\n+++ b/../outside.txt\n@@ -0,0 +1 @@\n+bad\n"));

    EXPECT_TRUE(preview.value("success", false));
    EXPECT_FALSE(preview.value("can_apply", true));
    EXPECT_EQ(preview["validation"].value("error_type", ""), "path_outside_workspace");
    EXPECT_FALSE(preview["validation"].value("paths_inside_workspace", true));
}

TEST_F(PatchServiceTest, PreviewValidatedDetectsContextMismatch) {
    auto file = dir() / "file.txt";
    write_text(file, "hello\nactual");
    ben_gear::patch::PatchService service(make_ctx(dir()));

    auto preview = patch_result_json(service.preview_validated("--- a/file.txt\n+++ b/file.txt\n@@ -1,2 +1,2 @@\n hello\n-old\n+new\n"));

    EXPECT_TRUE(preview.value("success", false));
    EXPECT_FALSE(preview.value("can_apply", true));
    EXPECT_EQ(preview["validation"].value("error_type", ""), "patch_conflict");
    EXPECT_FALSE(preview["validation"].value("hunks_match", true));
}

TEST_F(PatchServiceTest, PreviewValidatedDoesNotModifyFilesOrCreateChanges) {
    auto file = dir() / "file.txt";
    write_text(file, "hello\nold");
    ben_gear::patch::PatchService service(make_ctx(dir()));

    auto preview = patch_result_json(service.preview_validated("--- a/file.txt\n+++ b/file.txt\n@@ -1,2 +1,2 @@\n hello\n-old\n+new\n"));

    EXPECT_TRUE(preview.value("success", false));
    EXPECT_EQ(read_text(file), "hello\nold");
    auto changes = patch_result_json(service.list_changes());
    ASSERT_TRUE(changes.value("success", false));
    EXPECT_TRUE(changes["changes"].empty());
}
