#pragma once

#include "server/api/common.hpp"

namespace ben_gear::server {

struct PermissionApiService {
    std::function<Json(const std::string& workspace,
                       const std::string& session_id,
                       const std::string& username)> list_pending;

    std::function<Json(const std::string& workspace,
                       const std::string& session_id,
                       const std::string& username,
                       std::string_view permission_id,
                       bool allow_session)> approve;

    std::function<Json(const std::string& workspace,
                       const std::string& session_id,
                       const std::string& username,
                       std::string_view permission_id)> deny;
};

} // namespace ben_gear::server
