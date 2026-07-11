#pragma once

#include "server/api/common.hpp"

namespace ben_gear::server {

struct GitApiService {
    std::function<Json(const container::String& workspace,
                       const container::String& username)> status;

    std::function<Json(const container::String& workspace,
                       const container::String& username,
                       std::string_view path,
                       bool staged,
                       bool stat,
                       bool preview)> diff;

    std::function<Json(const container::String& workspace,
                       const container::String& username,
                       std::string_view path,
                       int limit)> log;

    std::function<Json(const container::String& workspace,
                       const container::String& username)> branches;

    std::function<Json(const container::String& workspace,
                       const container::String& username)> worktrees;

    std::function<Json(const container::String& workspace,
                       const container::String& session_id,
                       const container::String& username,
                       std::string_view name,
                       std::string_view start_point,
                       bool force)> create_branch;

    std::function<Json(const container::String& workspace,
                       const container::String& session_id,
                       const container::String& username,
                       std::string_view name,
                       bool force)> switch_branch;

    std::function<Json(const container::String& workspace,
                       const container::String& session_id,
                       const container::String& username,
                       std::string_view name,
                       bool force)> delete_branch;

    std::function<Json(const container::String& workspace,
                       const container::String& session_id,
                       const container::String& username,
                       const std::vector<std::string>& paths,
                       bool staged,
                       bool worktree)> restore;

    std::function<Json(const container::String& workspace,
                       const container::String& session_id,
                       const container::String& username,
                       std::string_view message,
                       const std::vector<std::string>& paths,
                       bool all,
                       bool amend)> commit;
};

} // namespace ben_gear::server
