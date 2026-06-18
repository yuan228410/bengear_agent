#include "ben_gear/permission/policy_engine.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <utility>

namespace ben_gear::permission {

namespace {

std::string to_std(const base::container::String& value) {
    return std::string(value.data(), value.size());
}

std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

} // namespace

std::string to_string(PolicyEffect effect) {
    switch (effect) {
        case PolicyEffect::allow: return "allow";
        case PolicyEffect::ask: return "ask";
        case PolicyEffect::deny: return "deny";
    }
    return "deny";
}

Json to_json(const PermissionDecision& decision) {
    return Json{{"success", false},
                {"error_type", decision.effect == PolicyEffect::ask ? "permission_required" : "permission_denied"},
                {"policy_effect", to_string(decision.effect)},
                {"policy_key", decision.policy_key},
                {"message", decision.reason},
                {"resource", decision.resource}};
}

PolicyEngine::PolicyEngine(workspace::WorkspaceContext ws_ctx)
    : ws_ctx_(std::move(ws_ctx)) {}

void PolicyEngine::allow_for_session(std::string policy_key) {
    session_allow_.insert(std::move(policy_key));
}

std::filesystem::path PolicyEngine::project_root() const {
    if (!ws_ctx_.project_path.empty()) return std::filesystem::path(to_std(ws_ctx_.project_path));
    return std::filesystem::current_path();
}

PermissionDecision PolicyEngine::allow(std::string key, std::string reason) const {
    PermissionDecision decision;
    decision.effect = PolicyEffect::allow;
    decision.policy_key = std::move(key);
    decision.reason = std::move(reason);
    return decision;
}

PermissionDecision PolicyEngine::ask(std::string key, std::string reason, Json resource) const {
    PermissionDecision decision;
    decision.effect = session_allow_.count(key) ? PolicyEffect::allow : PolicyEffect::ask;
    decision.policy_key = std::move(key);
    decision.reason = std::move(reason);
    decision.resource = std::move(resource);
    return decision;
}

PermissionDecision PolicyEngine::deny(std::string key, std::string reason, Json resource) const {
    PermissionDecision decision;
    decision.effect = PolicyEffect::deny;
    decision.policy_key = std::move(key);
    decision.reason = std::move(reason);
    decision.resource = std::move(resource);
    return decision;
}

bool PolicyEngine::path_inside_workspace(const std::string& input, std::string& normalized) const {
    if (input.empty()) return true;
    std::filesystem::path path(input);
    std::error_code ec;
    auto root = std::filesystem::weakly_canonical(project_root(), ec);
    if (ec) return false;
    auto target = path.is_absolute() ? path : root / path;
    target = std::filesystem::weakly_canonical(target, ec);
    if (ec) target = std::filesystem::weakly_canonical(target.parent_path(), ec) / target.filename();
    normalized = target.string();
    auto root_text = root.string();
    auto target_text = target.string();
    return target_text == root_text || target_text.rfind(root_text + std::string(1, std::filesystem::path::preferred_separator), 0) == 0;
}

bool PolicyEngine::dangerous_shell_command(const std::string& command) const {
    auto lower = lower_copy(command);
    const char* patterns[] = {
        "rm -rf /",
        "sudo ",
        "mkfs",
        "dd if=",
        "chmod -r 777",
        "chown -r",
        ":(){ :|:& };:",
    };
    for (const auto* pattern : patterns) {
        if (lower.find(pattern) != std::string::npos) return true;
    }
    return false;
}

PermissionDecision PolicyEngine::evaluate_tool(std::string_view tool_name, const Json& arguments) const {
    return evaluate_tool_permission(tool_name, arguments);
}

PermissionDecision PolicyEngine::evaluate_tool_permission(std::string_view tool_name, const Json& arguments) const {
    std::string name(tool_name.data(), tool_name.size());

    if (name == "preview_diff" || name == "list_changes" || name == "read_change" ||
        name == "list_checkpoints" || name == "read_checkpoint" ||
        name == "inspect_test_commands" ||
        name == "git_status" || name == "git_diff" || name == "git_log" ||
        (name == "git_branch" && arguments.value("action", "list") == "list") ||
        (name == "git_worktree" && arguments.value("action", "list") == "list") ||
        name == "read_file" || name == "list_directory" || name == "file_info" ||
        name == "search_files" || name == "grep_content" || name == "http_get" ||
        name == "get_skill" || name == "list_skills" || name == "read_memory" ||
        name == "recall" || name == "read_soul" || name == "read_rules") {
        return allow(name, "read-only tool");
    }

    if (arguments.is_object()) {
        if (arguments.contains("path") && arguments["path"].is_string()) {
            std::string normalized;
            auto path = arguments["path"].get<std::string>();
            if (!path_inside_workspace(path, normalized)) {
                return deny("path.outside_workspace", "tool path escapes workspace", Json{{"path", path}, {"normalized", normalized}});
            }
        }
        if (arguments.contains("paths") && arguments["paths"].is_array()) {
            for (const auto& item : arguments["paths"]) {
                if (!item.is_string()) continue;
                std::string normalized;
                auto path = item.get<std::string>();
                if (!path_inside_workspace(path, normalized)) {
                    return deny("path.outside_workspace", "tool path escapes workspace", Json{{"path", path}, {"normalized", normalized}});
                }
            }
        }
        if (arguments.contains("location") && arguments["location"].is_string()) {
            std::string normalized;
            auto path = arguments["location"].get<std::string>();
            if (!path_inside_workspace(path, normalized)) {
                return deny("path.outside_workspace", "tool location escapes workspace", Json{{"location", path}, {"normalized", normalized}});
            }
        }
    }

    if (name == "execute_command") {
        auto command = arguments.value("command", "");
        if (dangerous_shell_command(command)) {
            return deny("shell.dangerous", "dangerous shell command blocked", Json{{"command", command}});
        }
        return ask("shell.default", "shell command requires approval", Json{{"command", command}});
    }
    if (name == "run_tests") {
        auto command = arguments.value("command", "");
        if (dangerous_shell_command(command)) {
            return deny("shell.dangerous", "dangerous test command blocked", Json{{"command", command}});
        }
        return ask("test.run", "running tests executes a workspace command", Json{{"command", command}});
    }

    if (name == "apply_patch") {
        return ask("patch.apply", "applying a patch modifies workspace files");
    }
    if (name == "revert_patch") {
        return ask("patch.revert", "reverting a patch modifies workspace files");
    }
    if (name == "create_checkpoint") {
        return ask("checkpoint.create", "creating a checkpoint reads workspace file contents", Json{{"paths", arguments.value("paths", Json::array())}});
    }
    if (name == "restore_checkpoint") {
        return ask("checkpoint.restore", "restoring a checkpoint modifies workspace files", Json{{"checkpoint_id", arguments.value("checkpoint_id", "")}});
    }
    if (name == "delete_checkpoint") {
        return ask("checkpoint.delete", "deleting a checkpoint removes recovery data", Json{{"checkpoint_id", arguments.value("checkpoint_id", "")}});
    }
    if (name == "git_restore") {
        return ask("git.restore", "git restore modifies workspace files");
    }
    if (name == "git_branch") {
        return ask("git.branch", "git branch mutation requires approval", Json{{"action", arguments.value("action", "list")}});
    }
    if (name == "git_commit") {
        return ask("git.commit", "git commit modifies repository history", Json{{"message", arguments.value("message", "")}});
    }
    if (name == "git_worktree") {
        return ask("git.worktree", "git worktree mutation requires approval", Json{{"action", arguments.value("action", "list")}});
    }

    if (name == "write_file" || name == "delete_file" || name == "rename_file" ||
        name == "copy_file" || name == "mkdir") {
        return ask("file.write", "file mutation requires approval", Json{{"tool", name}});
    }

    return allow(name, "no restrictive policy matched");
}

} // namespace ben_gear::permission
