#pragma once

#include "ben_gear/server/api/common.hpp"

namespace ben_gear::server {

struct RepoMapApiService {
    std::function<Json(const container::String& workspace,
                       const container::String& username)> overview;

    std::function<Json(const container::String& workspace,
                       const container::String& username,
                       std::string_view query,
                       std::string_view kind,
                       std::string_view language,
                       int limit)> find_files;

    std::function<Json(const container::String& workspace,
                       const container::String& username,
                       std::string_view query,
                       std::string_view kind,
                       std::string_view language,
                       int limit)> find_symbols;

    std::function<Json(const container::String& workspace,
                       const container::String& username,
                       std::string_view path)> explain_path;
};

} // namespace ben_gear::server
