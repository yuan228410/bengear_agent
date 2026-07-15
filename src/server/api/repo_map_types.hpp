#pragma once

#include "server/api/common.hpp"

namespace ben_gear::server {

struct RepoMapApiService {
    std::function<Json(const std::string& workspace,
                       const std::string& username)> overview;

    std::function<Json(const std::string& workspace,
                       const std::string& username,
                       std::string_view query,
                       std::string_view kind,
                       std::string_view language,
                       int limit)> find_files;

    std::function<Json(const std::string& workspace,
                       const std::string& username,
                       std::string_view query,
                       std::string_view kind,
                       std::string_view language,
                       int limit)> find_symbols;

    std::function<Json(const std::string& workspace,
                       const std::string& username,
                       std::string_view path)> explain_path;
};

} // namespace ben_gear::server
