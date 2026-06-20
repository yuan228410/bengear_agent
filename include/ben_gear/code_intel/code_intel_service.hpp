#pragma once

#include "ben_gear/code_intel/types.hpp"
#include "ben_gear/repo_map/repo_map_service.hpp"
#include "ben_gear/workspace/types.hpp"

#include <filesystem>
#include <memory>
#include <string_view>

namespace ben_gear::code_intel {

class CodeIntelService {
public:
    explicit CodeIntelService(workspace::WorkspaceContext ws_ctx,
                              std::shared_ptr<repo_map::RepoMapService> repo_map_service = nullptr);

    Json capabilities() const;
    Json document_symbols(std::string_view path) const;
    Json workspace_symbols(std::string_view query,
                           std::string_view kind = {},
                           std::string_view language = {},
                           int limit = 50,
                           const CodeIntelOptions& options = {}) const;
    Json definition(const CodeIntelQuery& query, const CodeIntelOptions& options = {}) const;
    Json references(const CodeIntelQuery& query, const CodeIntelOptions& options = {}) const;

private:
    repo_map::RepoMapService::Options repo_map_options(const CodeIntelOptions& options) const;
    std::filesystem::path project_root() const;
    bool validate_relative_path(std::string_view input, std::string& normalized, std::string& error) const;
    std::string symbol_from_query(const CodeIntelQuery& query, std::string& error) const;

    workspace::WorkspaceContext ws_ctx_;
    std::shared_ptr<repo_map::RepoMapService> repo_map_service_;
};

} // namespace ben_gear::code_intel
