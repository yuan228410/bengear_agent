#include "ben_gear/git/git_service.hpp"

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

std::string to_std(const base::container::String& value) {
    return std::string(value.data(), value.size());
}

Json error_json(std::string_view type, std::string_view message) {
    return Json{{"success", false}, {"error_type", std::string(type)}, {"message", std::string(message)}};
}

std::string shell_quote(const std::string& value) {
    std::string out = "'";
    for (char ch : value) {
        if (ch == '\'') out += "'\\''";
        else out.push_back(ch);
    }
    out += "'";
    return out;
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

Json parse_log_output(const std::string& output) {
    Json commits = Json::array();
    for (const auto& line : split_lines(output)) {
        if (line.empty()) continue;
        auto fields = split_fields(line, '\t');
        if (fields.size() < 5) continue;
        commits.push_back(Json{{"hash", fields[0]},
                               {"short_hash", fields[1]},
                               {"author", fields[2]},
                               {"date", fields[3]},
                               {"subject", fields[4]}});
    }
    return commits;
}

Json parse_branch_output(const std::string& output) {
    Json branches = Json::array();
    for (const auto& line : split_lines(output)) {
        if (line.empty()) continue;
        auto fields = split_fields(line, '\t');
        if (fields.size() < 3) continue;
        branches.push_back(Json{{"name", fields[0]},
                                {"current", fields.size() > 1 && fields[1] == "*"},
                                {"hash", fields.size() > 2 ? fields[2] : ""},
                                {"upstream", fields.size() > 3 ? fields[3] : ""}});
    }
    return branches;
}

Json parse_worktree_output(const std::string& output) {
    Json worktrees = Json::array();
    Json current = Json::object();
    for (const auto& line : split_lines(output)) {
        if (line.empty()) {
            if (!current.empty()) {
                worktrees.push_back(current);
                current = Json::object();
            }
            continue;
        }
        auto space = line.find(' ');
        auto key = space == std::string::npos ? line : line.substr(0, space);
        auto value = space == std::string::npos ? std::string() : line.substr(space + 1);
        if (key == "worktree") current["path"] = std::filesystem::path(value).lexically_normal().string();
        else if (key == "HEAD") current["head"] = value;
        else if (key == "branch") current["branch"] = value;
        else if (key == "bare") current["bare"] = true;
        else if (key == "detached") current["detached"] = true;
        else if (key == "prunable") current["prunable"] = value.empty() ? true : Json(value);
    }
    if (!current.empty()) worktrees.push_back(current);
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

GitService::GitService(workspace::WorkspaceContext ws_ctx)
    : ws_ctx_(std::move(ws_ctx)) {}

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
    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        result.output = "failed to start git";
        return result;
    }
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) {
        result.output += buffer.data();
    }
    int rc = pclose(pipe);
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
    if (!root.output.empty() && root.output.back() == '\n') root.output.pop_back();
    status.repo_root = root.output;

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

Json GitService::diff(const std::string& path, bool staged, bool stat) const {
    std::vector<std::string> args{"diff"};
    if (staged) args.push_back("--cached");
    if (stat) args.push_back("--stat");
    std::string normalized;
    std::string path_error;
    if (!validate_path(path, normalized, path_error)) return error_json("path_outside_workspace", path_error);
    if (!normalized.empty()) {
        args.push_back("--");
        args.push_back(normalized);
    }
    auto out = run_git(args);
    if (out.exit_code != 0) return error_json("git_command_failed", out.output);
    return Json{{"success", true}, {"diff", out.output}, {"staged", staged}, {"stat", stat}};
}

Json GitService::log(int limit, const std::string& path) const {
    if (limit <= 0) limit = 20;
    if (limit > 200) limit = 200;
    std::vector<std::string> args{"log", "--date=iso-strict", "--pretty=format:%H%x09%h%x09%an%x09%ad%x09%s", "-n", std::to_string(limit)};
    std::string normalized;
    std::string path_error;
    if (!validate_path(path, normalized, path_error)) return error_json("path_outside_workspace", path_error);
    if (!normalized.empty()) {
        args.push_back("--");
        args.push_back(normalized);
    }
    auto out = run_git(args);
    if (out.exit_code != 0) return error_json("git_command_failed", out.output);
    return Json{{"success", true}, {"limit", limit}, {"commits", parse_log_output(out.output)}};
}

Json GitService::branch(const std::string& action, const std::string& name, const std::string& start_point, bool force) const {
    auto mode = action.empty() ? "list" : action;
    if (mode == "list") {
        auto out = run_git({"branch", "--format=%(refname:short)	%(HEAD)	%(objectname:short)	%(upstream:short)", "--list"});
        if (out.exit_code != 0) return error_json("git_command_failed", out.output);
        return Json{{"success", true}, {"action", mode}, {"branches", parse_branch_output(out.output)}};
    }

    std::string branch_error;
    if (!validate_branch_name(name, branch_error)) return error_json("invalid_arguments", branch_error);

    std::vector<std::string> args;
    if (mode == "create") {
        args = {"branch"};
        if (force) args.push_back("-f");
        args.push_back(name);
        if (!start_point.empty()) args.push_back(start_point);
    } else if (mode == "switch") {
        args = {"switch"};
        if (force) args.push_back("--force");
        args.push_back(name);
    } else if (mode == "delete") {
        args = {"branch", force ? "-D" : "-d", name};
    } else {
        return error_json("invalid_arguments", "unsupported branch action");
    }

    auto out = run_git(args);
    if (out.exit_code != 0) return error_json("git_command_failed", out.output);
    return Json{{"success", true}, {"action", mode}, {"branch", name}, {"output", out.output}};
}

Json GitService::commit(const std::string& message, const std::vector<std::string>& paths, bool all, bool amend) const {
    auto trimmed = trim(message);
    if (trimmed.empty()) return error_json("invalid_arguments", "commit message is required");
    if (!paths.empty() && all) return error_json("invalid_arguments", "paths and all cannot be used together");

    std::vector<std::string> normalized_paths;
    std::string path_error;
    if (!validate_paths(paths, normalized_paths, path_error)) return error_json("path_outside_workspace", path_error);

    if (!normalized_paths.empty()) {
        std::vector<std::string> add_args{"add", "--"};
        add_args.insert(add_args.end(), normalized_paths.begin(), normalized_paths.end());
        auto add = run_git(add_args);
        if (add.exit_code != 0) return error_json("git_command_failed", add.output);
    }

    std::vector<std::string> args{"commit", "-m", trimmed};
    if (all) args.push_back("--all");
    if (amend) args.push_back("--amend");
    auto out = run_git(args);
    if (out.exit_code != 0) return error_json("git_command_failed", out.output);

    auto hash = run_git({"rev-parse", "HEAD"});
    auto short_hash = run_git({"rev-parse", "--short", "HEAD"});
    auto clean = [](std::string value) {
        if (!value.empty() && value.back() == '\n') value.pop_back();
        return value;
    };
    return Json{{"success", true},
                {"hash", hash.exit_code == 0 ? clean(hash.output) : ""},
                {"short_hash", short_hash.exit_code == 0 ? clean(short_hash.output) : ""},
                {"message", trimmed},
                {"output", out.output}};
}

Json GitService::restore(const std::vector<std::string>& paths, bool staged, bool worktree) const {
    if (paths.empty()) return error_json("invalid_arguments", "paths must be non-empty");
    std::vector<std::string> args{"restore"};
    if (staged) args.push_back("--staged");
    if (worktree) args.push_back("--worktree");
    args.push_back("--");
    Json restored = Json::array();
    for (const auto& path : paths) {
        std::string normalized;
        std::string path_error;
        if (!validate_path(path, normalized, path_error)) return error_json("path_outside_workspace", path_error);
        args.push_back(normalized);
        restored.push_back(normalized);
    }
    auto out = run_git(args);
    if (out.exit_code != 0) return error_json("git_command_failed", out.output);
    return Json{{"success", true}, {"restored", restored}, {"staged", staged}, {"worktree", worktree}};
}

Json GitService::worktree(const std::string& action, const std::string& location, const std::string& branch, bool create_branch, bool force) const {
    auto mode = action.empty() ? "list" : action;
    if (mode == "list") {
        auto out = run_git({"worktree", "list", "--porcelain"});
        if (out.exit_code != 0) return error_json("git_command_failed", out.output);
        return Json{{"success", true}, {"action", mode}, {"worktrees", parse_worktree_output(out.output)}};
    }

    if (mode == "prune") {
        auto out = run_git({"worktree", "prune"});
        if (out.exit_code != 0) return error_json("git_command_failed", out.output);
        return Json{{"success", true}, {"action", mode}, {"output", out.output}};
    }

    std::string normalized_location;
    std::string location_error;
    if (!validate_worktree_location(location, normalized_location, location_error)) return error_json("path_outside_workspace", location_error);

    std::vector<std::string> args;
    if (mode == "add") {
        args = {"worktree", "add"};
        if (force) args.push_back("--force");
        if (!branch.empty()) {
            std::string branch_error;
            if (!validate_branch_name(branch, branch_error)) return error_json("invalid_arguments", branch_error);
            if (create_branch) {
                args.push_back("-b");
                args.push_back(branch);
            }
        }
        args.push_back(normalized_location);
        if (!branch.empty() && !create_branch) args.push_back(branch);
    } else if (mode == "remove") {
        args = {"worktree", "remove"};
        if (force) args.push_back("--force");
        args.push_back(normalized_location);
    } else {
        return error_json("invalid_arguments", "unsupported worktree action");
    }

    auto out = run_git(args);
    if (out.exit_code != 0) return error_json("git_command_failed", out.output);
    return Json{{"success", true}, {"action", mode}, {"location", normalized_location}, {"output", out.output}};
}

} // namespace ben_gear::git
