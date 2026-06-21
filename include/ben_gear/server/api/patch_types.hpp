#pragma once

#include "ben_gear/server/api/common.hpp"

namespace ben_gear::server {

struct PatchApiService {
    std::function<Json(const container::String& workspace,
                       const container::String& session_id,
                       const container::String& username,
                       std::string_view unified_diff)> preview_patch;

    std::function<Json(const container::String& workspace,
                       const container::String& session_id,
                       const container::String& username,
                       std::string_view unified_diff,
                       std::string_view description)> apply_patch;

    std::function<Json(const container::String& workspace,
                       const container::String& session_id,
                       const container::String& username)> list_changes;

    std::function<Json(const container::String& workspace,
                       const container::String& session_id,
                       const container::String& username,
                       std::string_view change_id)> read_change;

    std::function<Json(const container::String& workspace,
                       const container::String& session_id,
                       const container::String& username,
                       std::string_view change_id,
                       bool force)> revert_change;
};

} // namespace ben_gear::server
