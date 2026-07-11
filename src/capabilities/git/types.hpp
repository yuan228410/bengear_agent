#pragma once

#include "base/utils/json.hpp"

#include <string>
#include <vector>

namespace ben_gear::git {

struct GitStatusEntry {
    std::string path;
    std::string xy;
    bool staged = false;
    bool unstaged = false;
    bool untracked = false;
};

struct GitStatus {
    bool success = false;
    std::string error_type;
    std::string message;
    std::string repo_root;
    std::string branch;
    bool clean = true;
    std::vector<GitStatusEntry> entries;
};

struct GitDiffResult {
    std::string diff;
    bool staged = false;
    bool stat = false;
};

struct GitCommitInfo {
    std::string hash;
    std::string short_hash;
    std::string author;
    std::string date;
    std::string subject;
};

struct GitLogResult {
    int limit = 20;
    std::vector<GitCommitInfo> commits;
};

struct GitBranchInfo {
    std::string name;
    bool current = false;
    std::string hash;
    std::string upstream;
};

struct GitBranchListResult {
    std::vector<GitBranchInfo> branches;
};

struct GitBranchMutationResult {
    std::string action;
    std::string branch;
    std::string output;
};

struct GitCommitResult {
    std::string hash;
    std::string short_hash;
    std::string message;
    std::string output;
};

struct GitRestoreResult {
    std::vector<std::string> restored;
    bool staged = false;
    bool worktree = true;
};

struct GitWorktreeInfo {
    std::string path;
    std::string head;
    std::string branch;
    bool bare = false;
    bool detached = false;
    bool prunable = false;
    std::string prunable_reason;
};

struct GitWorktreeListResult {
    std::vector<GitWorktreeInfo> worktrees;
};

struct GitWorktreeMutationResult {
    std::string action;
    std::string location;
    std::string output;
};

Json to_json(const GitStatusEntry& entry);
Json to_json(const GitStatus& status);
Json to_json(const GitDiffResult& result);
Json to_json(const GitCommitInfo& commit);
Json to_json(const GitLogResult& result);
Json to_json(const GitBranchInfo& branch);
Json to_json(const GitBranchListResult& result);
Json to_json(const GitBranchMutationResult& result);
Json to_json(const GitCommitResult& result);
Json to_json(const GitRestoreResult& result);
Json to_json(const GitWorktreeInfo& worktree);
Json to_json(const GitWorktreeListResult& result);
Json to_json(const GitWorktreeMutationResult& result);

} // namespace ben_gear::git
