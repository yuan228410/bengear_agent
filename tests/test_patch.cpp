#include "ben_gear/patch/patch_service.hpp"
#include "ben_gear/test/test_framework.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

using bengear::test::TmpDirTest;

class PatchServiceTest : public TmpDirTest {};

namespace {

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
    ctx.project_path = ben_gear::base::container::String(root.string().c_str());
    ctx.session_id = ben_gear::base::container::String("patch-test-session");
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

TEST_F(PatchServiceTest, ApplyAndRevertSimpleModify) {
    auto file = dir() / "file.txt";
    write_text(file, "hello\nold");
    ben_gear::patch::PatchService service(make_ctx(dir()));

    auto applied = service.apply("--- a/file.txt\n+++ b/file.txt\n@@ -1,2 +1,2 @@\n hello\n-old\n+new\n", "test change");
    EXPECT_TRUE(applied.value("success", false));
    EXPECT_EQ(read_text(file), "hello\nnew");

    auto reverted = service.revert(applied.value("change_id", ""));
    EXPECT_TRUE(reverted.value("success", false));
    EXPECT_EQ(read_text(file), "hello\nold");
}

TEST_F(PatchServiceTest, ListAndReadChanges) {
    auto file = dir() / "file.txt";
    write_text(file, "hello\nold");
    ben_gear::patch::PatchService service(make_ctx(dir()));

    auto applied = service.apply("--- a/file.txt\n+++ b/file.txt\n@@ -1,2 +1,2 @@\n hello\n-old\n+new\n", "inspectable change");
    ASSERT_TRUE(applied.value("success", false));
    auto changes = service.list_changes();
    EXPECT_TRUE(changes.value("success", false));
    ASSERT_EQ(changes["changes"].size(), 1u);
    EXPECT_EQ(changes["changes"][0].value("description", ""), "inspectable change");

    auto change = service.read_change(applied.value("change_id", ""));
    EXPECT_TRUE(change.value("success", false));
    EXPECT_EQ(change["change"].value("description", ""), "inspectable change");
}

TEST_F(PatchServiceTest, ConflictDoesNotModifyFile) {
    auto file = dir() / "file.txt";
    write_text(file, "hello\nactual");
    ben_gear::patch::PatchService service(make_ctx(dir()));

    auto applied = service.apply("--- a/file.txt\n+++ b/file.txt\n@@ -1,2 +1,2 @@\n hello\n-old\n+new\n", "conflict");
    EXPECT_FALSE(applied.value("success", true));
    EXPECT_EQ(applied.value("error_type", ""), "patch_conflict");
    EXPECT_EQ(read_text(file), "hello\nactual");
}

TEST_F(PatchServiceTest, RejectPathTraversal) {
    ben_gear::patch::PatchService service(make_ctx(dir()));
    auto applied = service.apply("--- /dev/null\n+++ b/../outside.txt\n@@ -0,0 +1 @@\n+bad\n", "escape");
    EXPECT_FALSE(applied.value("success", true));
    EXPECT_EQ(applied.value("error_type", ""), "path_outside_workspace");
}
