#pragma once

#include "server/api/common.hpp"

#include <filesystem>
#include <string>

namespace ben_gear::server {

using GetUserDirFn = std::function<std::filesystem::path(const std::string& username)>;
using ListSessionsFn = std::function<std::vector<Json>(const std::string& workspace, const std::string& username)>;
using ListSessionsByWorkspaceFn = std::function<std::vector<Json>(const std::string& workspace_name, const std::string& username)>;
using CreateSessionFn = std::function<std::string(const std::string& name, const std::string& workspace, const std::string& username)>;
using DeleteSessionFn = std::function<bool(const std::string& session_id, const std::string& workspace, const std::string& username)>;
using RenameSessionFn = std::function<bool(const std::string& session_id, const std::string& name, const std::string& workspace, const std::string& username)>;
using LoadHistoryFn = std::function<std::vector<Json>(const std::string& session_id, const std::string& workspace, int limit, const std::string& username)>;
using ExportHistoryFn = std::function<std::string(const std::string& session_id, const std::string& workspace, bool include_tool_calls, bool include_thinking, bool include_tool_results, int limit, const std::string& username)>;

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

} // namespace ben_gear::server
