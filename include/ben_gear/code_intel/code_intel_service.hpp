#pragma once

#include "ben_gear/code_intel/types.hpp"
#include "ben_gear/domain/result.hpp"
#include "ben_gear/repo_map/repo_map_service.hpp"
#include "ben_gear/workspace/types.hpp"
#include "ben_gear/workspace_index/request_index_session.hpp"

#include <filesystem>
#include <memory>
#include <string_view>

namespace ben_gear::code_intel {

class CodeIntelService {
public:
    explicit CodeIntelService(workspace::WorkspaceContext ws_ctx,
                              std::shared_ptr<repo_map::RepoMapService> repo_map_service = nullptr);

    domain::AppResult<CodeIntelCapabilitiesResult> capabilities() const;
    domain::AppResult<CodeIntelDocumentSymbolsResult> document_symbols(std::string_view path) const;
    domain::AppResult<CodeIntelDocumentSymbolsResult> document_symbols(std::string_view path,
                                                                       workspace_index::RequestIndexSession& request_session) const;
    domain::AppResult<CodeIntelWorkspaceSymbolsResult> workspace_symbols(std::string_view query,
                                                                         std::string_view kind = {},
                                                                         std::string_view language = {},
                                                                         int limit = 50,
                                                                         const CodeIntelOptions& options = {}) const;
    domain::AppResult<CodeIntelWorkspaceSymbolsResult> workspace_symbols(std::string_view query,
                                                                         std::string_view kind,
                                                                         std::string_view language,
                                                                         int limit,
                                                                         const CodeIntelOptions& options,
                                                                         workspace_index::RequestIndexSession& request_session) const;
    domain::AppResult<CodeIntelDefinitionResult> definition(const CodeIntelQuery& query, const CodeIntelOptions& options = {}) const;
    domain::AppResult<CodeIntelDefinitionResult> definition(const CodeIntelQuery& query,
                                                            const CodeIntelOptions& options,
                                                            workspace_index::RequestIndexSession& request_session) const;
    domain::AppResult<CodeIntelReferencesResult> references(const CodeIntelQuery& query, const CodeIntelOptions& options = {}) const;
    domain::AppResult<CodeIntelReferencesResult> references(const CodeIntelQuery& query,
                                                            const CodeIntelOptions& options,
                                                            workspace_index::RequestIndexSession& request_session) const;
    workspace_index::RequestIndexSession request_session() const;

private:
    repo_map::RepoMapService::Options repo_map_options(const CodeIntelOptions& options) const;
    std::filesystem::path project_root() const;
    bool validate_relative_path(std::string_view input, std::string& normalized, std::string& error) const;
    std::string symbol_from_query(const CodeIntelQuery& query, std::string& error) const;

    workspace::WorkspaceContext ws_ctx_;
    std::shared_ptr<repo_map::RepoMapService> repo_map_service_;
};

} // namespace ben_gear::code_intel
