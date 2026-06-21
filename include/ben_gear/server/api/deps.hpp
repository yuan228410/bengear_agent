#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/vector.hpp"
#include "ben_gear/base/utils/json.hpp"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ben_gear::server {

namespace container = base::container;

// ---- 会话服务 ----
using GetUserDirFn = std::function<std::filesystem::path(const container::String& username)>;
using ListSessionsFn = std::function<container::Vector<Json>(const container::String& workspace, const container::String& username)>;
using ListSessionsByWorkspaceFn = std::function<container::Vector<Json>(const container::String& workspace_name, const container::String& username)>;
using CreateSessionFn = std::function<container::String(const container::String& name, const container::String& workspace, const container::String& username)>;
using DeleteSessionFn = std::function<bool(const container::String& session_id, const container::String& workspace, const container::String& username)>;
using RenameSessionFn = std::function<bool(const container::String& session_id, const container::String& name, const container::String& workspace, const container::String& username)>;
using LoadHistoryFn = std::function<container::Vector<Json>(const container::String& session_id, const container::String& workspace, int limit, const container::String& username)>;
using ExportHistoryFn = std::function<std::string(const container::String& session_id, const container::String& workspace, bool include_tool_calls, bool include_thinking, bool include_tool_results, int limit, const container::String& username)>;

struct SessionService {
    GetUserDirFn get_user_dir;
    ListSessionsFn list_sessions;
    ListSessionsByWorkspaceFn list_sessions_by_workspace;
    CreateSessionFn create_session;
    DeleteSessionFn delete_session;
    RenameSessionFn rename_session;
    LoadHistoryFn load_history;
    ExportHistoryFn export_history;
};

// ---- 配置服务 ----
struct ConfigInfo {
    container::String model;
    container::String provider;
    container::String workspace;
    container::String display_name;
    container::String version;
};

using GetConfigFn = std::function<ConfigInfo()>;
using SetModelFn = std::function<void(const container::String& model)>;

struct ConfigService {
    GetConfigFn get_config;
    SetModelFn set_model;
};

// ---- 工作空间服务 ----
struct WorkspaceInfo {
    container::String name;
    std::string path;
};

using ListWorkspacesFn = std::function<container::Vector<WorkspaceInfo>(const container::String& username)>;
using CreateWorkspaceFn = std::function<std::optional<WorkspaceInfo>(const container::String& name, const container::String& project_path, const container::String& username)>;
using DeleteWorkspaceFn = std::function<bool(const container::String& name, const container::String& username)>;

struct WorkspaceService {
    ListWorkspacesFn list_workspaces;
    CreateWorkspaceFn create_workspace;
    DeleteWorkspaceFn delete_workspace;
};

// ---- MCP 服务 ----
using GetMcpStatusFn = std::function<std::string()>;

struct McpService {
    GetMcpStatusFn get_status;
};


// ---- 聊天服务（OpenAI 兼容） ----
using ChatFn = std::function<void(const container::String& session_id,
                                    const container::String& prompt,
                                    const std::string& request_id,
                                    bool stream)>;

struct ChatService {
    ChatFn chat;
};

// ---- Git 服务 ----
struct GitApiService {
    std::function<Json(const container::String& workspace,
                       const container::String& session_id,
                       const container::String& username,
                       std::string_view tool_name,
                       const Json& arguments)> check_permission;

    std::function<Json(const container::String& workspace,
                       const container::String& username)> status;

    std::function<Json(const container::String& workspace,
                       const container::String& username,
                       std::string_view path,
                       bool staged,
                       bool stat,
                       bool preview)> diff;

    std::function<Json(const container::String& workspace,
                       const container::String& username,
                       std::string_view path,
                       int limit)> log;

    std::function<Json(const container::String& workspace,
                       const container::String& username)> branches;

    std::function<Json(const container::String& workspace,
                       const container::String& username)> worktrees;

    std::function<Json(const container::String& workspace,
                       const container::String& session_id,
                       const container::String& username,
                       std::string_view name,
                       std::string_view start_point,
                       bool force)> create_branch;

    std::function<Json(const container::String& workspace,
                       const container::String& session_id,
                       const container::String& username,
                       std::string_view name,
                       bool force)> switch_branch;

    std::function<Json(const container::String& workspace,
                       const container::String& session_id,
                       const container::String& username,
                       std::string_view name,
                       bool force)> delete_branch;

    std::function<Json(const container::String& workspace,
                       const container::String& session_id,
                       const container::String& username,
                       const std::vector<std::string>& paths,
                       bool staged,
                       bool worktree)> restore;

    std::function<Json(const container::String& workspace,
                       const container::String& session_id,
                       const container::String& username,
                       std::string_view message,
                       const std::vector<std::string>& paths,
                       bool all,
                       bool amend)> commit;
};

// ---- Diagnostic Repair Context 服务 ----
struct DiagnosticContextApiService {
    std::function<Json(const container::String& workspace,
                       const container::String& username,
                       const Json& request)> repair_context;
};

struct DiagnosticRepairApiService {
    std::function<Json(const container::String& workspace,
                       const container::String& username,
                       const Json& request)> repair_plan;

    std::function<Json(const container::String& workspace,
                       const container::String& username,
                       const Json& request)> repair_patch_preview;
};

// ---- Code Intelligence / LSP 服务 ----
struct CodeIntelApiService {
    std::function<Json(const container::String& workspace,
                       const container::String& username)> capabilities;

    std::function<Json(const container::String& workspace,
                       const container::String& username,
                       std::string_view path)> document_symbols;

    std::function<Json(const container::String& workspace,
                       const container::String& username,
                       std::string_view query,
                       std::string_view kind,
                       std::string_view language,
                       int limit)> workspace_symbols;

    std::function<Json(const container::String& workspace,
                       const container::String& username,
                       std::string_view path,
                       int line,
                       int column,
                       std::string_view symbol,
                       int limit)> definition;

    std::function<Json(const container::String& workspace,
                       const container::String& username,
                       std::string_view path,
                       int line,
                       int column,
                       std::string_view symbol,
                       int limit)> references;
};

// ---- Audit / Governance 服务 ----
struct AuditApiService {
    std::function<Json(const container::String& workspace,
                       const container::String& session_id,
                       const container::String& username,
                       const container::String& category,
                       const container::String& action,
                       int limit)> list_events;
};

// ---- Permission / Approval 服务 ----
struct PermissionApiService {
    std::function<Json(const container::String& workspace,
                       const container::String& session_id,
                       const container::String& username)> list_pending;

    std::function<Json(const container::String& workspace,
                       const container::String& session_id,
                       const container::String& username,
                       std::string_view permission_id,
                       bool allow_session)> approve;

    std::function<Json(const container::String& workspace,
                       const container::String& session_id,
                       const container::String& username,
                       std::string_view permission_id)> deny;
};

// ---- Checkpoint / Undo 服务 ----
struct CheckpointApiService {
    std::function<Json(const container::String& workspace,
                       const container::String& session_id,
                       const container::String& username,
                       std::string_view tool_name,
                       const Json& arguments)> check_permission;

    std::function<Json(const container::String& workspace,
                       const container::String& session_id,
                       const container::String& username)> list;

    std::function<Json(const container::String& workspace,
                       const container::String& session_id,
                       const container::String& username,
                       std::string_view checkpoint_id)> read;

    std::function<Json(const container::String& workspace,
                       const container::String& session_id,
                       const container::String& username,
                       std::string_view checkpoint_id,
                       const std::vector<std::string>& paths,
                       bool force)> restore;

    std::function<Json(const container::String& workspace,
                       const container::String& session_id,
                       const container::String& username,
                       std::string_view checkpoint_id)> remove;
};

struct TestLoopApiService {
    std::function<Json(const container::String& workspace,
                       const container::String& session_id,
                       const container::String& username,
                       std::string_view tool_name,
                       const Json& arguments)> check_permission;

    std::function<Json(const container::String& workspace,
                       const container::String& username)> inspect;

    std::function<Json(const container::String& workspace,
                       const container::String& session_id,
                       const container::String& username,
                       std::string_view command,
                       std::string_view cwd,
                       int timeout_seconds,
                       int max_output_bytes)> run;
};

// ---- Repo Map 服务 ----
struct RepoMapApiService {
    std::function<Json(const container::String& workspace,
                       const container::String& username)> overview;

    std::function<Json(const container::String& workspace,
                       const container::String& username,
                       std::string_view query,
                       std::string_view kind,
                       std::string_view language,
                       int limit)> find_files;

    std::function<Json(const container::String& workspace,
                       const container::String& username,
                       std::string_view query,
                       std::string_view kind,
                       std::string_view language,
                       int limit)> find_symbols;

    std::function<Json(const container::String& workspace,
                       const container::String& username,
                       std::string_view path)> explain_path;
};

struct PatchApiService {
    std::function<Json(const container::String& workspace,
                       const container::String& session_id,
                       const container::String& username,
                       std::string_view tool_name,
                       const Json& arguments)> check_permission;

    std::function<Json(const container::String& workspace,
                       const container::String& session_id,
                       const container::String& username,
                       std::string_view unified_diff)> preview_patch;

    std::function<Json(const container::String& workspace,
                       const container::String& session_id,
                       const container::String& username,
                       std::string_view unified_diff,
                       std::string_view description)> apply_patch;

    std::function<Json(const container::String& workspace,
                       const container::String& session_id,
                       const container::String& username)> list_changes;

    std::function<Json(const container::String& workspace,
                       const container::String& session_id,
                       const container::String& username,
                       std::string_view change_id)> read_change;

    std::function<Json(const container::String& workspace,
                       const container::String& session_id,
                       const container::String& username,
                       std::string_view change_id,
                       bool force)> revert_change;
};

} // namespace ben_gear::server
