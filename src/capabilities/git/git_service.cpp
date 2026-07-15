#include "capabilities/git/git_service.hpp"
#include "base/platform/os.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <sstream>
#include <string>
#include <string_view>

namespace ben_gear::git {

namespace {

namespace container = base::container;

std::string to_std(const std::string& value) {
    return value;
}

domain::AppError app_error(domain::AppErrorCategory category, std::string_view code, std::string_view message) {
    std::string error_code(code.data(), code.size());
    std::string error_message(message.data(), message.size());
    switch (category) {
    case domain::AppErrorCategory::invalid_argument:
        return domain::AppError::invalid_argument(std::move(error_code), std::move(error_message));
    case domain::AppErrorCategory::not_found:
        return domain::AppError::not_found(std::move(error_code), std::move(error_message));
    case domain::AppErrorCategory::permission_denied:
        return domain::AppError::permission_denied(std::move(error_code), std::move(error_message));
    case domain::AppErrorCategory::conflict:
        return domain::AppError::conflict(std::move(error_code), std::move(error_message));
    case domain::AppErrorCategory::unavailable:
        return domain::AppError::unavailable(std::move(error_code), std::move(error_message));
    case domain::AppErrorCategory::internal:
        return domain::AppError::internal(std::move(error_code), std::move(error_message));
    }
    return domain::AppError::internal(std::move(error_code), std::move(error_message));
}

domain::AppError invalid_argument(std::string_view code, std::string_view message) {
    return app_error(domain::AppErrorCategory::invalid_argument, code, message);
}

domain::AppError git_command_failed(std::string_view message) {
    return app_error(domain::AppErrorCategory::unavailable, "git_command_failed", message);
}

std::string shell_quote(const std::string& value) {
#if BEN_GEAR_PLATFORM_WINDOWS
    // 需要引号的情况：空格、制表符、换行符等特殊字符
    bool needs_quote = false;
    for (char ch : value) {
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '"') {
            needs_quote = true;
            break;
        }
    }
    if (!needs_quote) return value;
    return "\"" + value + "\"";
#else
    std::string out = "'";
    for (char ch : value) {
        if (ch == '\'') out += "'\\''";
        else out.push_back(ch);
    }
    out += "'";
    return out;
#endif
}

std::vector<std::string> split_lines(std::string_view text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < text.size()) {
        auto end = text.find('\n', start);
        if (end == std::string_view::npos) {
            lines.emplace_back(text.substr(start));
            break;
        }
        lines.emplace_back(text.substr(start, end - start));
        start = end + 1;
    }
    return lines;
}

std::vector<std::string> split_fields(std::string_view text, char delimiter) {
    std::vector<std::string> fields;
    size_t start = 0;
    while (start <= text.size()) {
        auto end = text.find(delimiter, start);
        if (end == std::string_view::npos) {
            fields.emplace_back(text.substr(start));
            break;
        }
        fields.emplace_back(text.substr(start, end - start));
        start = end + 1;
    }
    return fields;
}

std::string trim(std::string value) {
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

std::string trim_trailing_newline(std::string value) {
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r')) value.pop_back();
    return value;
}

std::vector<GitCommitInfo> parse_log_output(const std::string& output) {
    std::vector<GitCommitInfo> commits;
    for (const auto& line : split_lines(output)) {
        if (line.empty()) continue;
        auto fields = split_fields(line, '\t');
        if (fields.size() < 5) continue;
        GitCommitInfo commit;
        commit.hash = fields[0];
        commit.short_hash = fields[1];
        commit.author = fields[2];
        commit.date = fields[3];
        commit.subject = fields[4];
        commits.push_back(std::move(commit));
    }
    return commits;
}

std::vector<GitBranchInfo> parse_branch_output(const std::string& output) {
    std::vector<GitBranchInfo> branches;
    for (const auto& line : split_lines(output)) {
        if (line.empty()) continue;
        auto fields = split_fields(line, '\t');
        if (fields.size() < 3) continue;
        GitBranchInfo branch;
        branch.name = fields[0];
        branch.current = fields.size() > 1 && fields[1] == "*";
        branch.hash = fields.size() > 2 ? fields[2] : "";
        branch.upstream = fields.size() > 3 ? fields[3] : "";
        branches.push_back(std::move(branch));
    }
    return branches;
}

std::vector<GitWorktreeInfo> parse_worktree_output(const std::string& output) {
    std::vector<GitWorktreeInfo> worktrees;
    GitWorktreeInfo current;
    bool has_current = false;
    for (const auto& line : split_lines(output)) {
        if (line.empty()) {
            if (has_current) {
                worktrees.push_back(std::move(current));
                current = GitWorktreeInfo{};
                has_current = false;
            }
            continue;
        }
        has_current = true;
        auto space = line.find(' ');
        auto key = space == std::string::npos ? line : line.substr(0, space);
        auto value = space == std::string::npos ? std::string() : line.substr(space + 1);
        if (key == "worktree") current.path = std::filesystem::path(value).lexically_normal().string();
        else if (key == "HEAD") current.head = value;
        else if (key == "branch") current.branch = value;
        else if (key == "bare") current.bare = true;
        else if (key == "detached") current.detached = true;
        else if (key == "prunable") {
            current.prunable = true;
            current.prunable_reason = value;
        }
    }
    if (has_current) worktrees.push_back(std::move(current));
    return worktrees;
}

} // namespace

Json to_json(const GitStatusEntry& entry) {
    return Json{{"path", entry.path}, {"xy", entry.xy}, {"staged", entry.staged}, {"unstaged", entry.unstaged}, {"untracked", entry.untracked}};
}

Json to_json(const GitStatus& status) {
    Json entries = Json::array();
    for (const auto& entry : status.entries) entries.push_back(to_json(entry));
    return Json{{"success", status.success}, {"error_type", status.error_type}, {"message", status.message}, {"repo_root", status.repo_root}, {"branch", status.branch}, {"clean", status.clean}, {"entries", entries}};
}

Json to_json(const GitDiffResult& result) {
    return Json{{"success", true}, {"diff", result.diff}, {"staged", result.staged}, {"stat", result.stat}};
}

Json to_json(const GitCommitInfo& commit) {
    return Json{{"hash", commit.hash}, {"short_hash", commit.short_hash}, {"author", commit.author}, {"date", commit.date}, {"subject", commit.subject}};
}

Json to_json(const GitLogResult& result) {
    Json commits = Json::array();
    for (const auto& commit : result.commits) commits.push_back(to_json(commit));
    return Json{{"success", true}, {"limit", result.limit}, {"commits", commits}};
}

Json to_json(const GitBranchInfo& branch) {
    return Json{{"name", branch.name}, {"current", branch.current}, {"hash", branch.hash}, {"upstream", branch.upstream}};
}

Json to_json(const GitBranchListResult& result) {
    Json branches = Json::array();
    for (const auto& branch : result.branches) branches.push_back(to_json(branch));
    return Json{{"success", true}, {"action", "list"}, {"branches", branches}};
}

Json to_json(const GitBranchMutationResult& result) {
    return Json{{"success", true}, {"action", result.action}, {"branch", result.branch}, {"output", result.output}};
}

Json to_json(const GitCommitResult& result) {
    return Json{{"success", true}, {"hash", result.hash}, {"short_hash", result.short_hash}, {"message", result.message}, {"output", result.output}};
}

Json to_json(const GitRestoreResult& result) {
    Json restored = Json::array();
    for (const auto& path : result.restored) restored.push_back(path);
    return Json{{"success", true}, {"restored", restored}, {"staged", result.staged}, {"worktree", result.worktree}};
}

Json to_json(const GitWorktreeInfo& worktree) {
    Json result{{"path", worktree.path}, {"head", worktree.head}, {"branch", worktree.branch}, {"bare", worktree.bare}, {"detached", worktree.detached}};
    if (worktree.prunable) {
        result["prunable"] = worktree.prunable_reason.empty() ? Json(true) : Json(worktree.prunable_reason);
    }
    return result;
}

Json to_json(const GitWorktreeListResult& result) {
    Json worktrees = Json::array();
    for (const auto& worktree : result.worktrees) worktrees.push_back(to_json(worktree));
    return Json{{"success", true}, {"action", "list"}, {"worktrees", worktrees}};
}

Json to_json(const GitWorktreeMutationResult& result) {
    return Json{{"success", true}, {"action", result.action}, {"location", result.location}, {"output", result.output}};
}

std::filesystem::path GitService::project_root() const {
    if (!ws_ctx_.project_path.empty()) return std::filesystem::path(to_std(ws_ctx_.project_path));
    return std::filesystem::current_path();
}

bool GitService::validate_path(const std::string& input, std::string& normalized, std::string& error) const {
    if (input.empty()) return true;
    std::filesystem::path path(input);
    if (path.is_absolute()) {
        error = "git paths must be relative to the workspace";
        return false;
    }
    for (const auto& part : path) {
        if (part == "..") {
            error = "git path escapes workspace";
            return false;
        }
    }
    normalized = path.generic_string();
    return true;
}

bool GitService::validate_paths(const std::vector<std::string>& inputs, std::vector<std::string>& normalized, std::string& error) const {
    normalized.clear();
    for (const auto& path : inputs) {
        std::string item;
        if (!validate_path(path, item, error)) return false;
        if (!item.empty()) normalized.push_back(std::move(item));
    }
    return true;
}

bool GitService::validate_branch_name(const std::string& name, std::string& error) const {
    if (name.empty()) {
        error = "branch name is required";
        return false;
    }
    if (name[0] == '-' || name.find("..") != std::string::npos || name.find('~') != std::string::npos ||
        name.find('^') != std::string::npos || name.find(':') != std::string::npos || name.find('?') != std::string::npos ||
        name.find('*') != std::string::npos || name.find('[') != std::string::npos || name.find('\\') != std::string::npos ||
        name.back() == '/' || name.back() == '.') {
        error = "invalid branch name";
        return false;
    }
    return true;
}

bool GitService::validate_worktree_location(const std::string& input, std::string& normalized, std::string& error) const {
    if (input.empty()) {
        error = "worktree location is required";
        return false;
    }
    std::filesystem::path path(input);
    if (path.is_absolute()) {
        error = "worktree location must be relative to the workspace parent";
        return false;
    }
    for (const auto& part : path) {
        if (part == "..") {
            error = "worktree location escapes workspace parent";
            return false;
        }
    }
    normalized = (project_root().parent_path() / path).string();
    return true;
}

GitService::CommandResult GitService::run_git(const std::vector<std::string>& args) const {
    std::string command = "git -C " + shell_quote(project_root().string());
    for (const auto& arg : args) command += " " + shell_quote(arg);
    command += " 2>&1";

    CommandResult result;
    std::array<char, 4096> buffer{};
    FILE* pipe = base::platform::compat::popen(command.c_str(), "r");
    if (!pipe) {
        result.output = "failed to start git";
        return result;
    }
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
        result.output += buffer.data();
    }
    int rc = base::platform::compat::pclose(pipe);
    result.exit_code = rc == -1 ? -1 : rc;
    return result;
}

GitStatus GitService::status() const {
    GitStatus status;
    auto root = run_git({"rev-parse", "--show-toplevel"});
    if (root.exit_code != 0) {
        status.error_type = "git_not_repo";
        status.message = root.output;
        return status;
    }
    status.repo_root = trim_trailing_newline(root.output);

    auto out = run_git({"status", "--porcelain=v1", "-b"});
    if (out.exit_code != 0) {
        status.error_type = "git_command_failed";
        status.message = out.output;
        return status;
    }

    for (const auto& line : split_lines(out.output)) {
        if (line.rfind("## ", 0) == 0) {
            status.branch = line.substr(3);
            continue;
        }
        if (line.size() < 3) continue;
        GitStatusEntry entry;
        entry.xy = line.substr(0, 2);
        entry.path = line.substr(3);
        entry.untracked = entry.xy == "??";
        entry.staged = !entry.untracked && entry.xy[0] != ' ';
        entry.unstaged = !entry.untracked && entry.xy.size() > 1 && entry.xy[1] != ' ';
        status.entries.push_back(std::move(entry));
    }
    status.clean = status.entries.empty();
    status.success = true;
    return status;
}

domain::AppResult<GitDiffResult> GitService::diff(const std::string& path, bool staged, bool stat) const {
    std::vector<std::string> args{"diff"};
    if (staged) args.push_back("--cached");
    if (stat) args.push_back("--stat");
    std::string normalized;
    std::string path_error;
    if (!validate_path(path, normalized, path_error)) return domain::AppResult<GitDiffResult>::failure(invalid_argument("path_outside_workspace", path_error));
    if (!normalized.empty()) {
        args.push_back("--");
        args.push_back(normalized);
    }
    auto out = run_git(args);
    if (out.exit_code != 0) return domain::AppResult<GitDiffResult>::failure(git_command_failed(out.output));
    return domain::AppResult<GitDiffResult>::success(GitDiffResult{out.output, staged, stat});
}

domain::AppResult<GitLogResult> GitService::log(int limit, const std::string& path) const {
    if (limit <= 0) limit = 20;
    if (limit > 200) limit = 200;
    std::vector<std::string> args{"log", "--date=iso-strict", "--pretty=format:%H%x09%h%x09%an%x09%ad%x09%s", "-n", std::to_string(limit)};
    std::string normalized;
    std::string path_error;
    if (!validate_path(path, normalized, path_error)) return domain::AppResult<GitLogResult>::failure(invalid_argument("path_outside_workspace", path_error));
    if (!normalized.empty()) {
        args.push_back("--");
        args.push_back(normalized);
    }
    auto out = run_git(args);
    if (out.exit_code != 0) return domain::AppResult<GitLogResult>::failure(git_command_failed(out.output));
    return domain::AppResult<GitLogResult>::success(GitLogResult{limit, parse_log_output(out.output)});
}

domain::AppResult<GitBranchListResult> GitService::list_branches() const {
    auto out = run_git({"branch", "--format=%(refname:short)\t%(HEAD)\t%(objectname:short)\t%(upstream:short)", "--list"});
    if (out.exit_code != 0) return domain::AppResult<GitBranchListResult>::failure(git_command_failed(out.output));
    return domain::AppResult<GitBranchListResult>::success(GitBranchListResult{parse_branch_output(out.output)});
}

domain::AppResult<GitBranchMutationResult> GitService::create_branch(const std::string& name, const std::string& start_point, bool force) const {
    std::string branch_error;
    if (!validate_branch_name(name, branch_error)) return domain::AppResult<GitBranchMutationResult>::failure(invalid_argument("invalid_arguments", branch_error));
    std::vector<std::string> args{"branch"};
    if (force) args.push_back("-f");
    args.push_back(name);
    if (!start_point.empty()) args.push_back(start_point);
    auto out = run_git(args);
    if (out.exit_code != 0) return domain::AppResult<GitBranchMutationResult>::failure(git_command_failed(out.output));
    return domain::AppResult<GitBranchMutationResult>::success(GitBranchMutationResult{"create", name, out.output});
}

domain::AppResult<GitBranchMutationResult> GitService::switch_branch(const std::string& name, bool force) const {
    std::string branch_error;
    if (!validate_branch_name(name, branch_error)) return domain::AppResult<GitBranchMutationResult>::failure(invalid_argument("invalid_arguments", branch_error));
    std::vector<std::string> args{"switch"};
    if (force) args.push_back("--force");
    args.push_back(name);
    auto out = run_git(args);
    if (out.exit_code != 0) return domain::AppResult<GitBranchMutationResult>::failure(git_command_failed(out.output));
    return domain::AppResult<GitBranchMutationResult>::success(GitBranchMutationResult{"switch", name, out.output});
}

domain::AppResult<GitBranchMutationResult> GitService::delete_branch(const std::string& name, bool force) const {
    std::string branch_error;
    if (!validate_branch_name(name, branch_error)) return domain::AppResult<GitBranchMutationResult>::failure(invalid_argument("invalid_arguments", branch_error));
    auto out = run_git({"branch", force ? "-D" : "-d", name});
    if (out.exit_code != 0) return domain::AppResult<GitBranchMutationResult>::failure(git_command_failed(out.output));
    return domain::AppResult<GitBranchMutationResult>::success(GitBranchMutationResult{"delete", name, out.output});
}

domain::AppResult<GitCommitResult> GitService::commit(const std::string& message, const std::vector<std::string>& paths, bool all, bool amend) const {
    auto trimmed = trim(message);
    if (trimmed.empty()) return domain::AppResult<GitCommitResult>::failure(invalid_argument("invalid_arguments", "commit message is required"));
    if (!paths.empty() && all) return domain::AppResult<GitCommitResult>::failure(invalid_argument("invalid_arguments", "paths and all cannot be used together"));

    std::vector<std::string> normalized_paths;
    std::string path_error;
    if (!validate_paths(paths, normalized_paths, path_error)) return domain::AppResult<GitCommitResult>::failure(invalid_argument("path_outside_workspace", path_error));

    if (!normalized_paths.empty()) {
        std::vector<std::string> add_args{"add", "--"};
        add_args.insert(add_args.end(), normalized_paths.begin(), normalized_paths.end());
        auto add = run_git(add_args);
        if (add.exit_code != 0) return domain::AppResult<GitCommitResult>::failure(git_command_failed(add.output));
    }

    std::vector<std::string> args{"commit", "-m", trimmed};
    if (all) args.push_back("--all");
    if (amend) args.push_back("--amend");
    auto out = run_git(args);
    if (out.exit_code != 0) return domain::AppResult<GitCommitResult>::failure(git_command_failed(out.output));

    auto hash = run_git({"rev-parse", "HEAD"});
    auto short_hash = run_git({"rev-parse", "--short", "HEAD"});
    GitCommitResult result;
    result.hash = hash.exit_code == 0 ? trim_trailing_newline(hash.output) : "";
    result.short_hash = short_hash.exit_code == 0 ? trim_trailing_newline(short_hash.output) : "";
    result.message = trimmed;
    result.output = out.output;
    return domain::AppResult<GitCommitResult>::success(std::move(result));
}

domain::AppResult<GitRestoreResult> GitService::restore(const std::vector<std::string>& paths, bool staged, bool worktree) const {
    if (paths.empty()) return domain::AppResult<GitRestoreResult>::failure(invalid_argument("invalid_arguments", "paths must be non-empty"));
    std::vector<std::string> args{"restore"};
    if (staged) args.push_back("--staged");
    if (worktree) args.push_back("--worktree");
    args.push_back("--");
    std::vector<std::string> restored;
    for (const auto& path : paths) {
        std::string normalized;
        std::string path_error;
        if (!validate_path(path, normalized, path_error)) return domain::AppResult<GitRestoreResult>::failure(invalid_argument("path_outside_workspace", path_error));
        args.push_back(normalized);
        restored.push_back(normalized);
    }
    auto out = run_git(args);
    if (out.exit_code != 0) return domain::AppResult<GitRestoreResult>::failure(git_command_failed(out.output));
    return domain::AppResult<GitRestoreResult>::success(GitRestoreResult{std::move(restored), staged, worktree});
}

domain::AppResult<GitWorktreeListResult> GitService::list_worktrees() const {
    auto out = run_git({"worktree", "list", "--porcelain"});
    if (out.exit_code != 0) return domain::AppResult<GitWorktreeListResult>::failure(git_command_failed(out.output));
    return domain::AppResult<GitWorktreeListResult>::success(GitWorktreeListResult{parse_worktree_output(out.output)});
}

domain::AppResult<GitWorktreeMutationResult> GitService::add_worktree(const std::string& location, const std::string& branch, bool create_branch, bool force) const {
    std::string normalized_location;
    std::string location_error;
    if (!validate_worktree_location(location, normalized_location, location_error)) {
        return domain::AppResult<GitWorktreeMutationResult>::failure(invalid_argument("path_outside_workspace", location_error));
    }

    std::vector<std::string> args{"worktree", "add"};
    if (force) args.push_back("--force");
    if (!branch.empty()) {
        std::string branch_error;
        if (!validate_branch_name(branch, branch_error)) return domain::AppResult<GitWorktreeMutationResult>::failure(invalid_argument("invalid_arguments", branch_error));
        if (create_branch) {
            args.push_back("-b");
            args.push_back(branch);
        }
    }
    args.push_back(normalized_location);
    if (!branch.empty() && !create_branch) args.push_back(branch);

    auto out = run_git(args);
    if (out.exit_code != 0) return domain::AppResult<GitWorktreeMutationResult>::failure(git_command_failed(out.output));
    return domain::AppResult<GitWorktreeMutationResult>::success(GitWorktreeMutationResult{"add", normalized_location, out.output});
}

domain::AppResult<GitWorktreeMutationResult> GitService::remove_worktree(const std::string& location, bool force) const {
    std::string normalized_location;
    std::string location_error;
    if (!validate_worktree_location(location, normalized_location, location_error)) {
        return domain::AppResult<GitWorktreeMutationResult>::failure(invalid_argument("path_outside_workspace", location_error));
    }
    std::vector<std::string> args{"worktree", "remove"};
    if (force) args.push_back("--force");
    args.push_back(normalized_location);
    auto out = run_git(args);
    if (out.exit_code != 0) return domain::AppResult<GitWorktreeMutationResult>::failure(git_command_failed(out.output));
    return domain::AppResult<GitWorktreeMutationResult>::success(GitWorktreeMutationResult{"remove", normalized_location, out.output});
}

domain::AppResult<GitWorktreeMutationResult> GitService::prune_worktrees() const {
    auto out = run_git({"worktree", "prune"});
    if (out.exit_code != 0) return domain::AppResult<GitWorktreeMutationResult>::failure(git_command_failed(out.output));
    return domain::AppResult<GitWorktreeMutationResult>::success(GitWorktreeMutationResult{"prune", "", out.output});
}

} // namespace ben_gear::git
