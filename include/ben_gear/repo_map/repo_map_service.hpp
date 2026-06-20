#pragma once

#include "ben_gear/git/git_service.hpp"
#include "ben_gear/repo_map/types.hpp"
#include "ben_gear/test_loop/test_loop_service.hpp"
#include "ben_gear/workspace/types.hpp"

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
                            std::shared_ptr<git::GitService> git_service = nullptr,
                            std::shared_ptr<test_loop::TestLoopService> test_loop_service = nullptr);

    RepoMapIndex snapshot() const;
    RepoMapIndex snapshot(const Options& options) const;
    Json overview() const;
    Json overview(const Options& options) const;
    Json find_files(const std::string& query,
                    const std::string& kind = {},
                    const std::string& language = {},
                    int limit = 50) const;
    Json find_files(const std::string& query,
                    const std::string& kind,
                    const std::string& language,
                    int limit,
                    const Options& options) const;
    Json find_symbols(const std::string& query,
                      const std::string& kind = {},
                      const std::string& language = {},
                      int limit = 50) const;
    Json find_symbols(const std::string& query,
                      const std::string& kind,
                      const std::string& language,
                      int limit,
                      const Options& options) const;
    Json explain_path(const std::string& path) const;
    Json explain_path(const std::string& path,
                      const Options& options) const;

private:
    std::filesystem::path project_root() const;
    bool validate_relative_path(const std::string& input, std::string& normalized, std::string& error) const;
    RepoMapIndex build_index(const Options& options) const;

    workspace::WorkspaceContext ws_ctx_;
    std::shared_ptr<git::GitService> git_service_;
    std::shared_ptr<test_loop::TestLoopService> test_loop_service_;
};

} // namespace ben_gear::repo_map
