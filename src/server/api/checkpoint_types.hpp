#pragma once

#include "server/api/common.hpp"

namespace ben_gear::server {

struct CheckpointApiService {
    std::function<Json(const container::String& workspace,
                       const container::String& session_id,
                       const container::String& username)> list;

    std::function<Json(const container::String& workspace,
                       const container::String& session_id,
                       const container::String& username,
                       std::string_view checkpoint_id)> read;

    std::function<Json(const container::String& workspace,
                       const container::String& session_id,
                       const container::String& username,
                       std::string_view checkpoint_id,
                       const std::vector<std::string>& paths,
                       bool force)> restore;

    std::function<Json(const container::String& workspace,
                       const container::String& session_id,
                       const container::String& username,
                       std::string_view checkpoint_id)> remove;
};

} // namespace ben_gear::server
