#pragma once

#include "ben_gear/server/api/common.hpp"

namespace ben_gear::server {

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

} // namespace ben_gear::server
