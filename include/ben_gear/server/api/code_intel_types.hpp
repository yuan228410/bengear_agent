#pragma once

#include "ben_gear/server/api/common.hpp"

namespace ben_gear::server {

struct CodeIntelApiService {
    std::function<Json(const container::String& workspace,
                       const container::String& username)> capabilities;

    std::function<Json(const container::String& workspace,
                       const container::String& username,
                       std::string_view path)> document_symbols;

    std::function<Json(const container::String& workspace,
                       const container::String& username,
                       std::string_view query,
                       std::string_view kind,
                       std::string_view language,
                       int limit)> workspace_symbols;

    std::function<Json(const container::String& workspace,
                       const container::String& username,
                       std::string_view path,
                       int line,
                       int column,
                       std::string_view symbol,
                       int limit)> definition;

    std::function<Json(const container::String& workspace,
                       const container::String& username,
                       std::string_view path,
                       int line,
                       int column,
                       std::string_view symbol,
                       int limit)> references;
};

} // namespace ben_gear::server
