#pragma once

#include "server/api/common.hpp"

namespace ben_gear::server {

struct PatchApiService {
    std::function<Json(const std::string& workspace,
                       const std::string& session_id,
                       const std::string& username,
                       std::string_view unified_diff)> preview_patch;

    std::function<Json(const std::string& workspace,
                       const std::string& session_id,
                       const std::string& username,
                       std::string_view unified_diff,
                       std::string_view description)> apply_patch;

    std::function<Json(const std::string& workspace,
                       const std::string& session_id,
                       const std::string& username,
                       std::string_view unified_diff,
                       std::string_view description,
                       std::string_view test_command,
                       std::string_view test_cwd,
                       int test_timeout_seconds,
                       int test_max_output_bytes)> safe_code_change;

    std::function<Json(const std::string& workspace,
                       const std::string& session_id,
                       const std::string& username)> list_changes;

    std::function<Json(const std::string& workspace,
                       const std::string& session_id,
                       const std::string& username,
                       std::string_view change_id)> read_change;

    std::function<Json(const std::string& workspace,
                       const std::string& session_id,
                       const std::string& username,
                       std::string_view change_id,
                       bool force)> revert_change;
};

} // namespace ben_gear::server
