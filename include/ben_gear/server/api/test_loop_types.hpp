#pragma once

#include "ben_gear/server/api/common.hpp"

namespace ben_gear::server {

struct TestLoopApiService {
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

} // namespace ben_gear::server
