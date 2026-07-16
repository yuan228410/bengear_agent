#pragma once

#include "server/api/common.hpp"

#include <filesystem>
#include <memory>
#include <string>

namespace ben_gear::server {

class SessionService {
public:
    virtual ~SessionService() = default;

    virtual std::filesystem::path get_user_dir(const std::string& username) = 0;
    virtual std::vector<Json> list_sessions(const std::string& workspace, const std::string& username) = 0;
    virtual std::vector<Json> list_sessions_by_workspace(const std::string& workspace_name, const std::string& username) = 0;
    virtual std::string create_session(const std::string& name, const std::string& workspace, const std::string& username) = 0;
    virtual bool delete_session(const std::string& session_id, const std::string& workspace, const std::string& username) = 0;
    virtual bool rename_session(const std::string& session_id, const std::string& name, const std::string& workspace, const std::string& username) = 0;
    virtual std::vector<Json> load_history(const std::string& session_id, const std::string& workspace, int limit, const std::string& username) = 0;
    virtual std::string export_history(const std::string& session_id, const std::string& workspace, bool include_tool_calls, bool include_thinking, bool include_tool_results, int limit, const std::string& username) = 0;
};

} // namespace ben_gear::server
