#include "ben_gear/code_intel/code_intelligence_index.hpp"

#include "ben_gear/domain/errors.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace ben_gear::code_intel {

namespace {

repo_map::RepoMapService::Options repo_options_from_code_options(const CodeIntelOptions& options) {
    repo_map::RepoMapService::Options repo_options;
    repo_options.max_files = options.max_files;
    repo_options.max_symbols = options.max_symbols;
    repo_options.max_dependencies = 0;
    repo_options.max_file_bytes = options.max_file_bytes;
    repo_options.include_external = options.include_external;
    repo_options.include_hidden = options.include_hidden;
    return repo_options;
}

bool contains_ci(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) return true;
    auto lower = [](char ch) { return static_cast<char>(std::tolower(static_cast<unsigned char>(ch))); };
    std::string h(haystack);
    std::string n(needle);
    std::transform(h.begin(), h.end(), h.begin(), lower);
    std::transform(n.begin(), n.end(), n.begin(), lower);
    return h.find(n) != std::string::npos;
}

template <class T>
bool clamp_push(std::vector<T>& out, T value, int limit, bool& truncated) {
    if (static_cast<int>(out.size()) >= limit) {
        truncated = true;
        return false;
    }
    out.push_back(std::move(value));
    return true;
}

} // namespace

CodeIntelligenceIndex::CodeIntelligenceIndex(workspace::WorkspaceContext ws_ctx,
                                             std::shared_ptr<repo_map::RepoMapService> repo_map_service,
                                             std::shared_ptr<CodeIntelService> code_intel_service)
    : repo_map_service_(std::move(repo_map_service)),
      code_intel_service_(std::move(code_intel_service)),
      request_session_(nullptr) {
    if (!repo_map_service_) repo_map_service_ = std::make_shared<repo_map::RepoMapService>(ws_ctx);
    if (!code_intel_service_) code_intel_service_ = std::make_shared<CodeIntelService>(std::move(ws_ctx), repo_map_service_);
    request_session_ = repo_map_service_->request_session();
}

workspace_index::WorkspaceIndexOptions CodeIntelligenceIndex::to_index_options(const repo_map::RepoMapService::Options& options) {
    workspace_index::WorkspaceIndexOptions index_options;
    index_options.max_files = options.max_files;
    index_options.max_symbols = options.max_symbols;
    index_options.max_dependencies = options.max_dependencies;
    index_options.max_file_bytes = options.max_file_bytes;
    index_options.include_external = options.include_external;
    index_options.include_hidden = options.include_hidden;
    index_options.refresh = options.refresh;
    return index_options;
}

repo_map::RepoMapIndex CodeIntelligenceIndex::snapshot(const repo_map::RepoMapService::Options& options) const {
    return repo_map_service_->snapshot(options, request_session_);
}

template <class T>
domain::AppResult<T> CodeIntelligenceIndex::index_error(const repo_map::RepoMapIndex& index) const {
    auto error = domain::AppError::unavailable(
        base::container::String(index.error_type.empty() ? "repo_map_index_failed" : index.error_type),
        base::container::String(index.message.empty() ? "repo map index failed" : index.message));
    error.details_json = repo_map::to_json(index).dump();
    return domain::AppResult<T>::failure(std::move(error));
}

domain::AppResult<repo_map::RepoMapOverviewResult> CodeIntelligenceIndex::overview(const repo_map::RepoMapService::Options& options) const {
    auto index = snapshot(options);
    if (!index.success) return index_error<repo_map::RepoMapOverviewResult>(index);

    repo_map::RepoMapOverviewResult result;
    result.summary = std::move(index.summary);
    for (const auto& file : index.files) {
        if (static_cast<int>(result.important_files.size()) >= 20) break;
        if (!file.skipped) result.important_files.push_back(file);
    }
    for (const auto& symbol : index.symbols) {
        if (static_cast<int>(result.important_symbols.size()) >= 50) break;
        result.important_symbols.push_back(symbol);
    }
    return domain::AppResult<repo_map::RepoMapOverviewResult>::success(std::move(result));
}

domain::AppResult<repo_map::RepoMapExplainPathResult> CodeIntelligenceIndex::explain_path(std::string_view path,
                                                                                          const repo_map::RepoMapService::Options& options) const {
    auto index = snapshot(options);
    if (!index.success) return index_error<repo_map::RepoMapExplainPathResult>(index);

    const std::string path_text(path);
    repo_map::RepoMapExplainPathResult result;
    result.summary = index.summary;
    bool found = false;
    for (const auto& file : index.files) {
        if (file.path == path_text) {
            result.file = file;
            found = true;
            break;
        }
    }
    if (!found) {
        auto error = domain::AppError::not_found(base::container::String("path_not_indexed"),
                                                 base::container::String("path is not indexed"));
        error.details_json = repo_map::to_json(index.summary).dump();
        return domain::AppResult<repo_map::RepoMapExplainPathResult>::failure(std::move(error));
    }
    for (const auto& symbol : index.symbols) {
        if (symbol.path == path_text) result.symbols.push_back(symbol);
    }
    for (const auto& dep : index.dependencies) {
        if (dep.from == path_text) result.dependencies.push_back(dep);
        if (dep.resolved_path == path_text || dep.target == path_text) result.dependents.push_back(dep);
    }
    for (const auto& file : index.files) {
        if (file.kind == repo_map::FileKind::test && contains_ci(file.path, result.file.path)) result.related_tests.push_back(file);
    }
    return domain::AppResult<repo_map::RepoMapExplainPathResult>::success(std::move(result));
}

domain::AppResult<repo_map::RepoMapFindFilesResult> CodeIntelligenceIndex::find_files(std::string_view query,
                                                                                      std::string_view kind,
                                                                                      std::string_view language,
                                                                                      int limit,
                                                                                      const repo_map::RepoMapService::Options& options) const {
    auto index = snapshot(options);
    if (!index.success) return index_error<repo_map::RepoMapFindFilesResult>(index);
    auto capped_limit = std::clamp(limit > 0 ? limit : 50, 1, 200);
    repo_map::RepoMapFindFilesResult result;
    result.summary = index.summary;
    for (const auto& file : index.files) {
        if (!query.empty() && !contains_ci(file.path, query)) continue;
        if (!kind.empty() && repo_map::to_string(file.kind) != kind) continue;
        if (!language.empty() && file.language != language) continue;
        if (!clamp_push(result.files, file, capped_limit, result.summary.truncated)) break;
    }
    return domain::AppResult<repo_map::RepoMapFindFilesResult>::success(std::move(result));
}

domain::AppResult<CodeIntelWorkspaceSymbolsResult> CodeIntelligenceIndex::workspace_symbols(std::string_view query,
                                                                                            std::string_view kind,
                                                                                            std::string_view language,
                                                                                            int limit,
                                                                                            const CodeIntelOptions& options) const {
    return code_intel_service_->workspace_symbols(query, kind, language, limit, options, request_session_);
}

domain::AppResult<CodeIntelDocumentSymbolsResult> CodeIntelligenceIndex::document_symbols(std::string_view path,
                                                                                          const CodeIntelOptions& options) const {
    auto index = snapshot(repo_options_from_code_options(options));
    if (!index.success) return index_error<CodeIntelDocumentSymbolsResult>(index);

    const std::string path_text(path);
    CodeIntelDocumentSymbolsResult result;
    result.path = path_text;
    for (const auto& symbol : index.symbols) {
        if (symbol.path == path_text) result.symbols.push_back(location_from_symbol(symbol));
    }
    return domain::AppResult<CodeIntelDocumentSymbolsResult>::success(std::move(result));
}

domain::AppResult<CodeIntelDefinitionResult> CodeIntelligenceIndex::definition(const CodeIntelQuery& query,
                                                                               const CodeIntelOptions& options) const {
    return code_intel_service_->definition(query, options, request_session_);
}

domain::AppResult<CodeIntelReferencesResult> CodeIntelligenceIndex::references(const CodeIntelQuery& query,
                                                                               const CodeIntelOptions& options) const {
    return code_intel_service_->references(query, options, request_session_);
}

} // namespace ben_gear::code_intel
