#pragma once

#include "ben_gear/server/api/common.hpp"

namespace ben_gear::server {

struct AuditApiService {
    std::function<Json(const container::String& workspace,
                       const container::String& session_id,
                       const container::String& username,
                       const container::String& category,
                       const container::String& action,
                       int limit)> list_events;
};

} // namespace ben_gear::server
