#pragma once

#include "ben_gear/code_intel/code_intel_service.hpp"
#include "ben_gear/repo_map/repo_map_service.hpp"
#include "ben_gear/workspace_index/request_index_session.hpp"

#include <memory>
#include <string_view>

namespace ben_gear::code_intel {

// Request-scoped facade over the shared indexed code-intelligence layer.
// Repo map and code-intel queries flow through the same RequestIndexSession so
// diagnostics, repair planning, and web adapters can compose queries without
// rebuilding independent indexes.
class CodeIntelligenceIndex {
public:
    explicit CodeIntelligenceIndex(workspace::WorkspaceContext ws_ctx,
                                   std::shared_ptr<repo_map::RepoMapService> repo_map_service = nullptr,
                                   std::shared_ptr<CodeIntelService> code_intel_service = nullptr);

    domain::AppResult<repo_map::RepoMapOverviewResult> overview(const repo_map::RepoMapService::Options& options = {}) const;
    domain::AppResult<repo_map::RepoMapExplainPathResult> explain_path(std::string_view path,
                                                                        const repo_map::RepoMapService::Options& options = {}) const;
    domain::AppResult<repo_map::RepoMapFindFilesResult> find_files(std::string_view query,
                                                                   std::string_view kind = {},
                                                                   std::string_view language = {},
                                                                   int limit = 50,
                                                                   const repo_map::RepoMapService::Options& options = {}) const;
    domain::AppResult<CodeIntelWorkspaceSymbolsResult> workspace_symbols(std::string_view query,
                                                                         std::string_view kind = {},
                                                                         std::string_view language = {},
                                                                         int limit = 50,
                                                                         const CodeIntelOptions& options = {}) const;
    domain::AppResult<CodeIntelDocumentSymbolsResult> document_symbols(std::string_view path,
                                                                       const CodeIntelOptions& options = {}) const;
    domain::AppResult<CodeIntelDefinitionResult> definition(const CodeIntelQuery& query,
                                                            const CodeIntelOptions& options = {}) const;
    domain::AppResult<CodeIntelReferencesResult> references(const CodeIntelQuery& query,
                                                            const CodeIntelOptions& options = {}) const;


private:
    static workspace_index::WorkspaceIndexOptions to_index_options(const repo_map::RepoMapService::Options& options);
    repo_map::RepoMapIndex snapshot(const repo_map::RepoMapService::Options& options) const;
    template <class T>
    domain::AppResult<T> index_error(const repo_map::RepoMapIndex& index) const;

    std::shared_ptr<repo_map::RepoMapService> repo_map_service_;
    std::shared_ptr<CodeIntelService> code_intel_service_;
    mutable workspace_index::RequestIndexSession request_session_;
};

} // namespace ben_gear::code_intel
