#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/vector.hpp"
#include "ben_gear/base/utils/json.hpp"

#include <functional>
#include <optional>
#include <string>

namespace ben_gear::server {

// ---- 会话服务 ----
using GetUserDirFn = std::function<std::filesystem::path(const container::String& username)>;
using ListSessionsFn = std::function<container::Vector<Json>(const container::String& workspace, const container::String& username)>;
using ListSessionsByWorkspaceFn = std::function<container::Vector<Json>(const container::String& workspace_name, const container::String& username)>;
using CreateSessionFn = std::function<container::String(const container::String& name, const container::String& workspace, const container::String& username)>;
using DeleteSessionFn = std::function<bool(const container::String& session_id, const container::String& workspace, const container::String& username)>;
using RenameSessionFn = std::function<bool(const container::String& session_id, const container::String& name, const container::String& workspace, const container::String& username)>;
using LoadHistoryFn = std::function<container::Vector<Json>(const container::String& session_id, const container::String& workspace, int limit, const container::String& username)>;

struct SessionService {
    GetUserDirFn get_user_dir;
    ListSessionsFn list_sessions;
    ListSessionsByWorkspaceFn list_sessions_by_workspace;
    CreateSessionFn create_session;
    DeleteSessionFn delete_session;
    RenameSessionFn rename_session;
    LoadHistoryFn load_history;
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

} // namespace ben_gear::server
