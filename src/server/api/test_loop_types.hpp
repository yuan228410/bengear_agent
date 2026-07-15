#pragma once

#include "server/api/common.hpp"

namespace ben_gear::server {

struct TestLoopApiService {
    std::function<Json(const std::string& workspace,
                       const std::string& username)> inspect;

    std::function<Json(const std::string& workspace,
                       const std::string& session_id,
                       const std::string& username,
                       std::string_view command,
                       std::string_view cwd,
                       int timeout_seconds,
                       int max_output_bytes)> run;
};

} // namespace ben_gear::server
