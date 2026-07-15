#pragma once

#include "server/api/common.hpp"

namespace ben_gear::server {

struct CheckpointApiService {
    std::function<Json(const std::string& workspace,
                       const std::string& session_id,
                       const std::string& username)> list;

    std::function<Json(const std::string& workspace,
                       const std::string& session_id,
                       const std::string& username,
                       std::string_view checkpoint_id)> read;

    std::function<Json(const std::string& workspace,
                       const std::string& session_id,
                       const std::string& username,
                       std::string_view checkpoint_id,
                       const std::vector<std::string>& paths,
                       bool force)> restore;

    std::function<Json(const std::string& workspace,
                       const std::string& session_id,
                       const std::string& username,
                       std::string_view checkpoint_id)> remove;
};

} // namespace ben_gear::server
