#include "ben_gear/permission/policy_engine.hpp"
#include "ben_gear/test/test_framework.hpp"

#include <filesystem>

using bengear::test::TmpDirTest;

class PermissionEngineTest : public TmpDirTest {};

namespace {

ben_gear::workspace::WorkspaceContext make_ctx(const std::filesystem::path& root) {
    ben_gear::workspace::WorkspaceContext ctx;
    ctx.project_path = ben_gear::base::container::String(root.string().c_str());
    return ctx;
}

} // namespace

TEST_F(PermissionEngineTest, AllowsReadOnlyTools) {
    ben_gear::permission::PolicyEngine engine(make_ctx(dir()));
    auto decision = engine.evaluate_tool("git_status", ben_gear::Json::object());
    EXPECT_TRUE(decision.allowed());
    EXPECT_EQ(decision.policy_key, "git_status");
    auto changes = engine.evaluate_tool("list_changes", ben_gear::Json::object());
    EXPECT_TRUE(changes.allowed());
    EXPECT_EQ(changes.policy_key, "list_changes");
    auto log = engine.evaluate_tool("git_log", ben_gear::Json::object());
    EXPECT_TRUE(log.allowed());
    EXPECT_EQ(log.policy_key, "git_log");
    auto branch_list = engine.evaluate_tool("git_branch", ben_gear::Json{{"action", "list"}});
    EXPECT_TRUE(branch_list.allowed());
    auto worktree_list = engine.evaluate_tool("git_worktree", ben_gear::Json{{"action", "list"}});
    EXPECT_TRUE(worktree_list.allowed());
}

TEST_F(PermissionEngineTest, AsksForPatchApplyByDefault) {
    ben_gear::permission::PolicyEngine engine(make_ctx(dir()));
    auto decision = engine.evaluate_tool("apply_patch", ben_gear::Json{{"unified_diff", ""}});
    EXPECT_FALSE(decision.allowed());
    EXPECT_EQ(ben_gear::permission::to_string(decision.effect), "ask");
    EXPECT_EQ(decision.policy_key, "patch.apply");
}

TEST_F(PermissionEngineTest, AsksForMutatingGitToolsByDefault) {
    ben_gear::permission::PolicyEngine engine(make_ctx(dir()));
    auto branch = engine.evaluate_tool("git_branch", ben_gear::Json{{"action", "create"}, {"name", "feature/test"}});
    EXPECT_FALSE(branch.allowed());
    EXPECT_EQ(ben_gear::permission::to_string(branch.effect), "ask");
    EXPECT_EQ(branch.policy_key, "git.branch");

    auto commit = engine.evaluate_tool("git_commit", ben_gear::Json{{"message", "update"}});
    EXPECT_FALSE(commit.allowed());
    EXPECT_EQ(commit.policy_key, "git.commit");

    auto worktree = engine.evaluate_tool("git_worktree", ben_gear::Json{{"action", "add"}, {"location", "../outside"}});
    EXPECT_FALSE(worktree.allowed());
    EXPECT_EQ(ben_gear::permission::to_string(worktree.effect), "deny");
    EXPECT_EQ(worktree.policy_key, "path.outside_workspace");
}

TEST_F(PermissionEngineTest, HandlesCheckpointPolicies) {
    ben_gear::permission::PolicyEngine engine(make_ctx(dir()));
    auto list = engine.evaluate_tool("list_checkpoints", ben_gear::Json::object());
    EXPECT_TRUE(list.allowed());
    auto read = engine.evaluate_tool("read_checkpoint", ben_gear::Json{{"checkpoint_id", "abc"}});
    EXPECT_TRUE(read.allowed());

    auto create = engine.evaluate_tool("create_checkpoint", ben_gear::Json{{"paths", ben_gear::Json::array({"file.txt"})}});
    EXPECT_FALSE(create.allowed());
    EXPECT_EQ(create.policy_key, "checkpoint.create");

    auto restore = engine.evaluate_tool("restore_checkpoint", ben_gear::Json{{"checkpoint_id", "abc"}});
    EXPECT_FALSE(restore.allowed());
    EXPECT_EQ(restore.policy_key, "checkpoint.restore");

    auto del = engine.evaluate_tool("delete_checkpoint", ben_gear::Json{{"checkpoint_id", "abc"}});
    EXPECT_FALSE(del.allowed());
    EXPECT_EQ(del.policy_key, "checkpoint.delete");
}

TEST_F(PermissionEngineTest, DeniesOutsideWorkspacePath) {
    ben_gear::permission::PolicyEngine engine(make_ctx(dir()));
    auto decision = engine.evaluate_tool("write_file", ben_gear::Json{{"path", "../outside.txt"}});
    EXPECT_FALSE(decision.allowed());
    EXPECT_EQ(ben_gear::permission::to_string(decision.effect), "deny");
    EXPECT_EQ(decision.policy_key, "path.outside_workspace");
}

TEST_F(PermissionEngineTest, DeniesDangerousShellCommand) {
    ben_gear::permission::PolicyEngine engine(make_ctx(dir()));
    auto decision = engine.evaluate_tool("execute_command", ben_gear::Json{{"command", "sudo rm -rf /"}});
    EXPECT_FALSE(decision.allowed());
    EXPECT_EQ(decision.policy_key, "shell.dangerous");
}

TEST_F(PermissionEngineTest, SessionAllowlistOverridesAsk) {
    ben_gear::permission::PolicyEngine engine(make_ctx(dir()));
    engine.allow_for_session("patch.apply");
    auto decision = engine.evaluate_tool("apply_patch", ben_gear::Json{{"unified_diff", ""}});
    EXPECT_TRUE(decision.allowed());
}
