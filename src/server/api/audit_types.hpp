#pragma once

#include "server/api/common.hpp"

namespace ben_gear::server {

struct AuditApiService {
    std::function<Json(const std::string& workspace,
                       const std::string& session_id,
                       const std::string& username,
                       const std::string& category,
                       const std::string& action,
                       int limit)> list_events;
};

} // namespace ben_gear::server
