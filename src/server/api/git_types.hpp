#pragma once

#include "server/api/common.hpp"

namespace ben_gear::server {

struct GitApiService {
    std::function<Json(const std::string& workspace,
                       const std::string& username)> status;

    std::function<Json(const std::string& workspace,
                       const std::string& username,
                       std::string_view path,
                       bool staged,
                       bool stat,
                       bool preview)> diff;

    std::function<Json(const std::string& workspace,
                       const std::string& username,
                       std::string_view path,
                       int limit)> log;

    std::function<Json(const std::string& workspace,
                       const std::string& username)> branches;

    std::function<Json(const std::string& workspace,
                       const std::string& username)> worktrees;

    std::function<Json(const std::string& workspace,
                       const std::string& session_id,
                       const std::string& username,
                       std::string_view name,
                       std::string_view start_point,
                       bool force)> create_branch;

    std::function<Json(const std::string& workspace,
                       const std::string& session_id,
                       const std::string& username,
                       std::string_view name,
                       bool force)> switch_branch;

    std::function<Json(const std::string& workspace,
                       const std::string& session_id,
                       const std::string& username,
                       std::string_view name,
                       bool force)> delete_branch;

    std::function<Json(const std::string& workspace,
                       const std::string& session_id,
                       const std::string& username,
                       const std::vector<std::string>& paths,
                       bool staged,
                       bool worktree)> restore;

    std::function<Json(const std::string& workspace,
                       const std::string& session_id,
                       const std::string& username,
                       std::string_view message,
                       const std::vector<std::string>& paths,
                       bool all,
                       bool amend)> commit;
};

} // namespace ben_gear::server
