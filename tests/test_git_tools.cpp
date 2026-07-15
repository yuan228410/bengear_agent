#include "capabilities/git/git_service.hpp"
#include "test_framework.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

using bengear::test::TmpDirTest;
using ben_gear::Json;

class GitServiceTest : public TmpDirTest {};

namespace {

Json git_result_json(const ben_gear::domain::AppResult<ben_gear::git::GitDiffResult>& result) {
    return result.ok() ? ben_gear::git::to_json(result.value()) : Json{{"success", false}, {"error_type", result.error().code}, {"message", result.error().message}};
}

Json git_result_json(const ben_gear::domain::AppResult<ben_gear::git::GitLogResult>& result) {
    return result.ok() ? ben_gear::git::to_json(result.value()) : Json{{"success", false}, {"error_type", result.error().code}, {"message", result.error().message}};
}

Json git_result_json(const ben_gear::domain::AppResult<ben_gear::git::GitBranchListResult>& result) {
    return result.ok() ? ben_gear::git::to_json(result.value()) : Json{{"success", false}, {"error_type", result.error().code}, {"message", result.error().message}};
}

Json git_result_json(const ben_gear::domain::AppResult<ben_gear::git::GitBranchMutationResult>& result) {
    return result.ok() ? ben_gear::git::to_json(result.value()) : Json{{"success", false}, {"error_type", result.error().code}, {"message", result.error().message}};
}

Json git_result_json(const ben_gear::domain::AppResult<ben_gear::git::GitCommitResult>& result) {
    return result.ok() ? ben_gear::git::to_json(result.value()) : Json{{"success", false}, {"error_type", result.error().code}, {"message", result.error().message}};
}

Json git_result_json(const ben_gear::domain::AppResult<ben_gear::git::GitRestoreResult>& result) {
    return result.ok() ? ben_gear::git::to_json(result.value()) : Json{{"success", false}, {"error_type", result.error().code}, {"message", result.error().message}};
}

Json git_result_json(const ben_gear::domain::AppResult<ben_gear::git::GitWorktreeListResult>& result) {
    return result.ok() ? ben_gear::git::to_json(result.value()) : Json{{"success", false}, {"error_type", result.error().code}, {"message", result.error().message}};
}

Json git_result_json(const ben_gear::domain::AppResult<ben_gear::git::GitWorktreeMutationResult>& result) {
    return result.ok() ? ben_gear::git::to_json(result.value()) : Json{{"success", false}, {"error_type", result.error().code}, {"message", result.error().message}};
}

void run_cmd(const std::filesystem::path& cwd, const std::string& command) {
    int rc;
#ifdef _WIN32
    // 用 git -C 替代 cd /d，避免 system() 链式命令在 MinGW 下的诡异行为
    // 如果不是 git 命令，回退到 cd /d
    if (command.rfind("git ", 0) == 0) {
        rc = std::system(("git -C \"" + cwd.string() + "\" " + command.substr(4) + " 2>&1").c_str());
    } else {
        rc = std::system(("cd /d \"" + cwd.string() + "\" && " + command + " 2>&1").c_str());
    }
#else
    rc = std::system(("cd '" + cwd.string() + "' && " + command + " >/dev/null 2>&1").c_str());
#endif
    ASSERT_EQ(rc, 0);
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
    return ctx;
}

void init_repo(const std::filesystem::path& root) {
    run_cmd(root, "git init");
    run_cmd(root, "git config user.email test@example.com");
    run_cmd(root, "git config user.name Test");
    run_cmd(root, "git config core.autocrlf false");
    write_text(root / "file.txt", "hello\n");
    run_cmd(root, "git add file.txt");
    run_cmd(root, "git commit -m init");
}

} // namespace

TEST_F(GitServiceTest, StatusCleanRepo) {
    init_repo(dir());
    ben_gear::git::GitService service(make_ctx(dir()));
    auto status = service.status();
    EXPECT_TRUE(status.success);
    EXPECT_TRUE(status.clean);
}

TEST_F(GitServiceTest, StatusDetectsModifiedAndUntracked) {
    init_repo(dir());
    write_text(dir() / "file.txt", "changed\n");
    write_text(dir() / "new.txt", "new\n");
    ben_gear::git::GitService service(make_ctx(dir()));
    auto status = service.status();
    EXPECT_TRUE(status.success);
    EXPECT_FALSE(status.clean);
    EXPECT_GE(status.entries.size(), 2u);
}

TEST_F(GitServiceTest, DiffAndRestoreFile) {
    init_repo(dir());
    write_text(dir() / "file.txt", "changed\n");
    ben_gear::git::GitService service(make_ctx(dir()));
    auto diff = git_result_json(service.diff("file.txt"));
    EXPECT_TRUE(diff.value("success", false));
    EXPECT_NE(diff.value("diff", "").find("changed"), std::string::npos);

    auto restored = git_result_json(service.restore({"file.txt"}));
    EXPECT_TRUE(restored.value("success", false));
    EXPECT_EQ(read_text(dir() / "file.txt"), "hello\n");
}

TEST_F(GitServiceTest, RestoreStagedKeepsWorktreeContent) {
    init_repo(dir());
    write_text(dir() / "file.txt", "changed\n");
    run_cmd(dir(), "git add file.txt");
    ben_gear::git::GitService service(make_ctx(dir()));

    auto restored = git_result_json(service.restore({"file.txt"}, true, false));
    EXPECT_TRUE(restored.value("success", false));
    EXPECT_EQ(read_text(dir() / "file.txt"), "changed\n");

    auto status = service.status();
    ASSERT_FALSE(status.entries.empty());
    EXPECT_FALSE(status.entries[0].staged);
    EXPECT_TRUE(status.entries[0].unstaged);
}

TEST_F(GitServiceTest, LogReturnsStructuredCommits) {
    init_repo(dir());
    ben_gear::git::GitService service(make_ctx(dir()));
    auto log = git_result_json(service.log(5));
    EXPECT_TRUE(log.value("success", false));
    ASSERT_GE(log["commits"].size(), 1u);
    EXPECT_EQ(log["commits"][0].value("subject", ""), "init");
}

TEST_F(GitServiceTest, BranchListCreateAndSwitch) {
    init_repo(dir());
    ben_gear::git::GitService service(make_ctx(dir()));

    auto created = git_result_json(service.create_branch("feature/test"));
    EXPECT_TRUE(created.value("success", false));

    auto listed = git_result_json(service.list_branches());
    EXPECT_TRUE(listed.value("success", false));
    bool found = false;
    for (const auto& branch : listed["branches"]) {
        if (branch.value("name", "") == "feature/test") found = true;
    }
    EXPECT_TRUE(found);

    auto switched = git_result_json(service.switch_branch("feature/test"));
    EXPECT_TRUE(switched.value("success", false));
    auto status = service.status();
    EXPECT_NE(status.branch.find("feature/test"), std::string::npos);
}

TEST_F(GitServiceTest, CommitStagesSelectedPaths) {
    init_repo(dir());
    write_text(dir() / "file.txt", "changed\n");
    ben_gear::git::GitService service(make_ctx(dir()));

    auto committed = git_result_json(service.commit("update file", {"file.txt"}));
    EXPECT_TRUE(committed.value("success", false));

    auto log = git_result_json(service.log(1));
    EXPECT_TRUE(log.value("success", false));
    ASSERT_GE(log["commits"].size(), 1u);
    EXPECT_EQ(log["commits"][0].value("subject", ""), "update file");
}

TEST_F(GitServiceTest, CommitRejectsEmptyMessage) {
    init_repo(dir());
    ben_gear::git::GitService service(make_ctx(dir()));

    auto committed = git_result_json(service.commit("   "));
    EXPECT_FALSE(committed.value("success", true));
    EXPECT_EQ(committed.value("error_type", ""), "invalid_arguments");
}

TEST_F(GitServiceTest, CommitRejectsPathsAndAll) {
    init_repo(dir());
    ben_gear::git::GitService service(make_ctx(dir()));

    auto committed = git_result_json(service.commit("update file", {"file.txt"}, true));
    EXPECT_FALSE(committed.value("success", true));
    EXPECT_EQ(committed.value("error_type", ""), "invalid_arguments");
}

TEST_F(GitServiceTest, CommitRejectsUnsafePaths) {
    init_repo(dir());
    ben_gear::git::GitService service(make_ctx(dir()));

    auto committed = git_result_json(service.commit("update outside", {"../outside.txt"}));
    EXPECT_FALSE(committed.value("success", true));
    EXPECT_EQ(committed.value("error_type", ""), "path_outside_workspace");
}

TEST_F(GitServiceTest, CommitSelectedPathsAlsoCommitsExistingIndex) {
    init_repo(dir());
    write_text(dir() / "file.txt", "pre-staged\n");
    run_cmd(dir(), "git add file.txt");
    write_text(dir() / "other.txt", "other\n");
    ben_gear::git::GitService service(make_ctx(dir()));

    auto committed = git_result_json(service.commit("commit index and selected", {"other.txt"}));
    EXPECT_TRUE(committed.value("success", false));

    auto file_show = git_result_json(service.diff("file.txt"));
    EXPECT_TRUE(file_show.value("success", false));
    EXPECT_EQ(file_show.value("diff", ""), "");

    auto other_show = git_result_json(service.diff("other.txt"));
    EXPECT_TRUE(other_show.value("success", false));
    EXPECT_EQ(other_show.value("diff", ""), "");
}

TEST_F(GitServiceTest, WorktreeListReturnsPrimaryWorktree) {
    init_repo(dir());
    ben_gear::git::GitService service(make_ctx(dir()));
    auto worktrees = git_result_json(service.list_worktrees());
    EXPECT_TRUE(worktrees.value("success", false));
    ASSERT_GE(worktrees["worktrees"].size(), 1u);
    bool found = false;
    auto expected = std::filesystem::weakly_canonical(dir()).string();
    for (const auto& worktree : worktrees["worktrees"]) {
        auto actual = std::filesystem::weakly_canonical(std::filesystem::path(worktree.value("path", "").c_str())).string();
        if (actual == expected) found = true;
    }
    EXPECT_TRUE(found);
}

TEST_F(GitServiceTest, WorktreeAddAndRemove) {
    init_repo(dir());
    ben_gear::git::GitService service(make_ctx(dir()));

    auto added = git_result_json(service.add_worktree("agent-worktree", "agent-worktree", true));
    EXPECT_TRUE(added.value("success", false));
    EXPECT_TRUE(std::filesystem::exists(dir().parent_path() / "agent-worktree"));

    auto removed = git_result_json(service.remove_worktree("agent-worktree", true));
    EXPECT_TRUE(removed.value("success", false));
    EXPECT_FALSE(std::filesystem::exists(dir().parent_path() / "agent-worktree"));
}

TEST_F(GitServiceTest, RejectsUnsafeGitPaths) {
    init_repo(dir());
    ben_gear::git::GitService service(make_ctx(dir()));
    auto diff = git_result_json(service.diff("../outside.txt"));
    EXPECT_FALSE(diff.value("success", true));
    EXPECT_EQ(diff.value("error_type", ""), "path_outside_workspace");
}

TEST_F(GitServiceTest, NonRepoReturnsStructuredError) {
    ben_gear::git::GitService service(make_ctx(dir()));
    auto status = service.status();
    EXPECT_FALSE(status.success);
    EXPECT_EQ(status.error_type, "git_not_repo");
}
