#pragma once

#include "ben_gear/server/api/common.hpp"

namespace ben_gear::server {

struct RuntimeApiService {
    std::function<Json(const container::String& workspace,
                       const container::String& session_id,
                       const container::String& username,
                       const container::String& action,
                       const container::String& status,
                       const container::String& capability,
                       int limit)> list_executions;
    std::function<Json(const container::String& username,
                       const container::String& execution_id)> read_execution;
};

} // namespace ben_gear::server
