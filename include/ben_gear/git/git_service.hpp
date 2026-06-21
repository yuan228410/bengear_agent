#pragma once

#include "ben_gear/domain/result.hpp"
#include "ben_gear/git/types.hpp"
#include "ben_gear/workspace/types.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace ben_gear::git {

class GitService {
public:
    explicit GitService(workspace::WorkspaceContext ws_ctx);

    GitStatus status() const;
    domain::AppResult<GitDiffResult> diff(const std::string& path = {}, bool staged = false, bool stat = false) const;
    domain::AppResult<GitLogResult> log(int limit = 20, const std::string& path = {}) const;

    domain::AppResult<GitBranchListResult> list_branches() const;
    domain::AppResult<GitBranchMutationResult> create_branch(const std::string& name,
                                                             const std::string& start_point = {},
                                                             bool force = false) const;
    domain::AppResult<GitBranchMutationResult> switch_branch(const std::string& name, bool force = false) const;
    domain::AppResult<GitBranchMutationResult> delete_branch(const std::string& name, bool force = false) const;

    domain::AppResult<GitCommitResult> commit(const std::string& message,
                                              const std::vector<std::string>& paths = {},
                                              bool all = false,
                                              bool amend = false) const;
    domain::AppResult<GitRestoreResult> restore(const std::vector<std::string>& paths,
                                                bool staged = false,
                                                bool worktree = true) const;

    domain::AppResult<GitWorktreeListResult> list_worktrees() const;
    domain::AppResult<GitWorktreeMutationResult> add_worktree(const std::string& location,
                                                              const std::string& branch = {},
                                                              bool create_branch = false,
                                                              bool force = false) const;
    domain::AppResult<GitWorktreeMutationResult> remove_worktree(const std::string& location, bool force = false) const;
    domain::AppResult<GitWorktreeMutationResult> prune_worktrees() const;

private:
    struct CommandResult {
        int exit_code = -1;
        std::string output;
    };

    std::filesystem::path project_root() const;
    bool validate_path(const std::string& input, std::string& normalized, std::string& error) const;
    bool validate_paths(const std::vector<std::string>& inputs, std::vector<std::string>& normalized, std::string& error) const;
    bool validate_branch_name(const std::string& name, std::string& error) const;
    bool validate_worktree_location(const std::string& input, std::string& normalized, std::string& error) const;
    CommandResult run_git(const std::vector<std::string>& args) const;

    workspace::WorkspaceContext ws_ctx_;
};

} // namespace ben_gear::git
