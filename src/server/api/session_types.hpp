#pragma once

#include "server/api/common.hpp"

#include <filesystem>
#include <string>

namespace ben_gear::server {

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

} // namespace ben_gear::server
