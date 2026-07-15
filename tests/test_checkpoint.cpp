#include "capabilities/checkpoint/checkpoint_service.hpp"
#include "test_framework.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

using bengear::test::TmpDirTest;
using ben_gear::Json;

class CheckpointServiceTest : public TmpDirTest {};

namespace {

Json checkpoint_result_json(const ben_gear::domain::AppResult<ben_gear::checkpoint::CheckpointCreateResult>& result) {
    return result.ok() ? ben_gear::checkpoint::to_json(result.value()) : Json{{"success", false}, {"error_type", result.error().code}, {"message", result.error().message}};
}

Json checkpoint_result_json(const ben_gear::domain::AppResult<ben_gear::checkpoint::CheckpointListResult>& result) {
    return result.ok() ? ben_gear::checkpoint::to_json(result.value()) : Json{{"success", false}, {"error_type", result.error().code}, {"message", result.error().message}};
}

Json checkpoint_result_json(const ben_gear::domain::AppResult<ben_gear::checkpoint::CheckpointReadResult>& result) {
    return result.ok() ? ben_gear::checkpoint::to_json(result.value()) : Json{{"success", false}, {"error_type", result.error().code}, {"message", result.error().message}};
}

Json checkpoint_result_json(const ben_gear::domain::AppResult<ben_gear::checkpoint::CheckpointRestoreResult>& result) {
    return result.ok() ? ben_gear::checkpoint::to_json(result.value()) : Json{{"success", false}, {"error_type", result.error().code}, {"message", result.error().message}};
}

Json checkpoint_result_json(const ben_gear::domain::AppResult<ben_gear::checkpoint::CheckpointRemoveResult>& result) {
    return result.ok() ? ben_gear::checkpoint::to_json(result.value()) : Json{{"success", false}, {"error_type", result.error().code}, {"message", result.error().message}};
}

void write_text(const std::filesystem::path& path, std::string_view text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

std::string read_text(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

ben_gear::workspace::WorkspaceContext make_ctx(const std::filesystem::path& root) {
    ben_gear::workspace::WorkspaceContext ctx;
    ctx.project_path = root.string();
    ctx.session_id = std::string("checkpoint-test-session");
    ctx.tier_paths.user_dir = root / ".bengear-test-user";
    return ctx;
}

} // namespace

TEST_F(CheckpointServiceTest, CreateListAndReadCheckpoint) {
    write_text(dir() / "file.txt", "before\n");
    ben_gear::checkpoint::CheckpointService service(make_ctx(dir()));

    auto created = checkpoint_result_json(service.create({"file.txt"}, "before edit"));
    ASSERT_TRUE(created.value("success", false));
    auto checkpoint_id = created.value("checkpoint_id", "");
    EXPECT_FALSE(checkpoint_id.empty());

    auto listed = checkpoint_result_json(service.list());
    EXPECT_TRUE(listed.value("success", false));
    ASSERT_EQ(listed["checkpoints"].size(), 1u);
    EXPECT_EQ(listed["checkpoints"][0].value("description", ""), "before edit");

    auto read = checkpoint_result_json(service.read(checkpoint_id));
    EXPECT_TRUE(read.value("success", false));
    ASSERT_EQ(read["checkpoint"]["files"].size(), 1u);
    EXPECT_EQ(read["checkpoint"]["files"][0].value("path", ""), "file.txt");
}

TEST_F(CheckpointServiceTest, RestoreExistingFileWithForce) {
    write_text(dir() / "file.txt", "before\n");
    ben_gear::checkpoint::CheckpointService service(make_ctx(dir()));
    auto created = checkpoint_result_json(service.create({"file.txt"}, "before edit"));
    ASSERT_TRUE(created.value("success", false));

    write_text(dir() / "file.txt", "after\n");
    auto restored = checkpoint_result_json(service.restore(created.value("checkpoint_id", ""), {}, true));
    EXPECT_TRUE(restored.value("success", false));
    EXPECT_EQ(read_text(dir() / "file.txt"), "before\n");
}

TEST_F(CheckpointServiceTest, DetectsRestoreConflictWithoutForce) {
    write_text(dir() / "file.txt", "before\n");
    ben_gear::checkpoint::CheckpointService service(make_ctx(dir()));
    auto created = checkpoint_result_json(service.create({"file.txt"}, "before edit"));
    ASSERT_TRUE(created.value("success", false));

    write_text(dir() / "file.txt", "after\n");
    auto restored = checkpoint_result_json(service.restore(created.value("checkpoint_id", "")));
    EXPECT_FALSE(restored.value("success", true));
    EXPECT_EQ(restored.value("error_type", ""), "checkpoint_conflict");
    EXPECT_EQ(read_text(dir() / "file.txt"), "after\n");
}

TEST_F(CheckpointServiceTest, RestoreDeletesFileThatDidNotExistAtCheckpoint) {
    ben_gear::checkpoint::CheckpointService service(make_ctx(dir()));
    auto created = checkpoint_result_json(service.create({"new.txt"}, "before create"));
    ASSERT_TRUE(created.value("success", false));

    write_text(dir() / "new.txt", "created\n");
    auto restored = checkpoint_result_json(service.restore(created.value("checkpoint_id", "")));
    EXPECT_TRUE(restored.value("success", false));
    EXPECT_FALSE(std::filesystem::exists(dir() / "new.txt"));
}

TEST_F(CheckpointServiceTest, RestoreSubsetOnlyChangesRequestedPaths) {
    write_text(dir() / "a.txt", "a-before\n");
    write_text(dir() / "b.txt", "b-before\n");
    ben_gear::checkpoint::CheckpointService service(make_ctx(dir()));
    auto created = checkpoint_result_json(service.create({"a.txt", "b.txt"}, "two files"));
    ASSERT_TRUE(created.value("success", false));

    write_text(dir() / "a.txt", "a-after\n");
    write_text(dir() / "b.txt", "b-after\n");
    auto restored = checkpoint_result_json(service.restore(created.value("checkpoint_id", ""), {"a.txt"}, true));
    EXPECT_TRUE(restored.value("success", false));
    EXPECT_EQ(read_text(dir() / "a.txt"), "a-before\n");
    EXPECT_EQ(read_text(dir() / "b.txt"), "b-after\n");
}

TEST_F(CheckpointServiceTest, DeleteCheckpointRemovesRecord) {
    write_text(dir() / "file.txt", "before\n");
    ben_gear::checkpoint::CheckpointService service(make_ctx(dir()));
    auto created = checkpoint_result_json(service.create({"file.txt"}, "temporary"));
    ASSERT_TRUE(created.value("success", false));

    auto removed = checkpoint_result_json(service.remove(created.value("checkpoint_id", "")));
    EXPECT_TRUE(removed.value("success", false));

    auto read = checkpoint_result_json(service.read(created.value("checkpoint_id", "")));
    EXPECT_FALSE(read.value("success", true));
    EXPECT_EQ(read.value("error_type", ""), "checkpoint_not_found");
}

TEST_F(CheckpointServiceTest, RejectsPathTraversal) {
    ben_gear::checkpoint::CheckpointService service(make_ctx(dir()));
    auto created = checkpoint_result_json(service.create({"../outside.txt"}, "escape"));
    EXPECT_FALSE(created.value("success", true));
    EXPECT_EQ(created.value("error_type", ""), "path_outside_workspace");
}
