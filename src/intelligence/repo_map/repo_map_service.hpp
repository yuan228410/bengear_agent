#pragma once

#include "domain/result.hpp"
#include "intelligence/repo_map/types.hpp"
#include "workspace/types.hpp"
#include "intelligence/workspace_index/request_index_session.hpp"
#include "intelligence/workspace_index/workspace_index_service.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace ben_gear::repo_map {

class RepoMapService {
public:
    struct Options {
        int max_files = 2000;
        int max_symbols = 5000;
        int max_dependencies = 5000;
        int max_file_bytes = 1024 * 1024;
        bool include_external = false;
        bool include_hidden = false;
        bool refresh = false;
    };

    explicit RepoMapService(workspace::WorkspaceContext ws_ctx,
                            std::shared_ptr<workspace_index::WorkspaceIndexService> index_service = nullptr);

    RepoMapIndex snapshot() const;
    RepoMapIndex snapshot(const Options& options) const;
    RepoMapIndex snapshot(const Options& options, workspace_index::RequestIndexSession& request_session) const;
    workspace_index::RequestIndexSession request_session() const;
    domain::AppResult<RepoMapOverviewResult> overview() const;
    domain::AppResult<RepoMapOverviewResult> overview(const Options& options) const;
    domain::AppResult<RepoMapFindFilesResult> find_files(const std::string& query,
                                                         const std::string& kind = {},
                                                         const std::string& language = {},
                                                         int limit = 50) const;
    domain::AppResult<RepoMapFindFilesResult> find_files(const std::string& query,
                                                         const std::string& kind,
                                                         const std::string& language,
                                                         int limit,
                                                         const Options& options) const;
    domain::AppResult<RepoMapFindSymbolsResult> find_symbols(const std::string& query,
                                                             const std::string& kind = {},
                                                             const std::string& language = {},
                                                             int limit = 50) const;
    domain::AppResult<RepoMapFindSymbolsResult> find_symbols(const std::string& query,
                                                             const std::string& kind,
                                                             const std::string& language,
                                                             int limit,
                                                             const Options& options) const;
    domain::AppResult<RepoMapExplainPathResult> explain_path(const std::string& path) const;
    domain::AppResult<RepoMapExplainPathResult> explain_path(const std::string& path,
                                                             const Options& options) const;

private:
    std::filesystem::path project_root() const;
    bool validate_relative_path(const std::string& input, std::string& normalized, std::string& error) const;
    RepoMapIndex build_index(const Options& options) const;
    RepoMapIndex scan_index(const Options& options) const;
    workspace_index::WorkspaceIndexOptions index_options(const Options& options) const;

    workspace::WorkspaceContext ws_ctx_;
    std::shared_ptr<workspace_index::WorkspaceIndexService> index_service_;
};

} // namespace ben_gear::repo_map
