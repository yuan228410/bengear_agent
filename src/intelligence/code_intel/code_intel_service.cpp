#include "intelligence/code_intel/code_intel_service.hpp"

#include "base/domain/errors.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ben_gear::code_intel {

namespace {

std::string to_std(const base::container::String& value) {
    return std::string(value.data(), value.size());
}

domain::AppError app_error(domain::AppErrorCategory category,
                           std::string_view code,
                           std::string_view message) {
    return domain::AppError{category,
                            base::container::String(code.data(), code.size()),
                            base::container::String(message.data(), message.size()),
                            base::container::String()};
}

template <class T>
domain::AppResult<T> repo_map_index_error(const repo_map::RepoMapIndex& index) {
    auto error = app_error(domain::AppErrorCategory::unavailable,
                           index.error_type.empty() ? "repo_map_index_failed" : index.error_type,
                           index.message.empty() ? "repo map index failed" : index.message);
    error.details_json = repo_map::to_json(index).dump();
    return domain::AppResult<T>::failure(std::move(error));
}

template <class T>
domain::AppResult<T> query_error(std::string_view code, std::string_view message) {
    auto error = app_error(domain::AppErrorCategory::invalid_argument, code, message);
    error.details_json = Json{{"success", false},
                              {"error_type", std::string(code)},
                              {"message", std::string(message)},
                              {"provider", "indexed"},
                              {"real_lsp", false}}
                             .dump();
    return domain::AppResult<T>::failure(std::move(error));
}

std::string trim(std::string value) {
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

std::string lower_copy(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return out;
}

bool is_identifier_char(char ch) {
    auto c = static_cast<unsigned char>(ch);
    return std::isalnum(c) || ch == '_';
}

bool is_identifier_start(char ch) {
    auto c = static_cast<unsigned char>(ch);
    return std::isalpha(c) || ch == '_';
}

bool valid_symbol_name(std::string_view symbol) {
    if (symbol.empty() || !is_identifier_start(symbol.front())) return false;
    return std::all_of(symbol.begin(), symbol.end(), is_identifier_char);
}

std::vector<std::string> split_lines(std::string_view text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < text.size()) {
        auto end = text.find('\n', start);
        if (end == std::string_view::npos) {
            lines.emplace_back(text.substr(start));
            break;
        }
        lines.emplace_back(text.substr(start, end - start));
        start = end + 1;
    }
    if (text.empty()) lines.emplace_back();
    return lines;
}

std::string read_file(const std::filesystem::path& path, std::string& error, int max_bytes = 1024 * 1024) {
    std::error_code ec;
    auto size = std::filesystem::file_size(path, ec);
    if (!ec && size > static_cast<std::uintmax_t>(max_bytes)) {
        error = "file_too_large";
        return {};
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "failed to read file";
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::string token_at_position(const std::vector<std::string>& lines, int line, int column) {
    if (line < 1 || line > static_cast<int>(lines.size())) return {};
    const auto& text = lines[static_cast<size_t>(line - 1)];
    if (text.empty()) return {};
    auto index = std::clamp(column - 1, 0, static_cast<int>(text.size() - 1));
    while (index > 0 && !is_identifier_char(text[static_cast<size_t>(index)]) && is_identifier_char(text[static_cast<size_t>(index - 1)])) --index;
    if (!is_identifier_char(text[static_cast<size_t>(index)])) return {};
    auto start = static_cast<size_t>(index);
    while (start > 0 && is_identifier_char(text[start - 1])) --start;
    auto end = static_cast<size_t>(index);
    while (end + 1 < text.size() && is_identifier_char(text[end + 1])) ++end;
    auto token = text.substr(start, end - start + 1);
    return valid_symbol_name(token) ? token : std::string();
}

std::string preview_for_line(const std::filesystem::path& root, const std::string& path, int line, int max_bytes) {
    std::string error;
    auto content = read_file(root / path, error, max_bytes);
    if (!error.empty()) return {};
    auto lines = split_lines(content);
    if (line < 1 || line > static_cast<int>(lines.size())) return {};
    return trim(lines[static_cast<size_t>(line - 1)]);
}

int symbol_score(const repo_map::RepoMapSymbol& symbol, const CodeIntelQuery& query) {
    int score = 0;
    if (!query.path.empty() && symbol.path == query.path) score += 25;
    if (symbol.kind == repo_map::SymbolKind::class_ || symbol.kind == repo_map::SymbolKind::struct_ || symbol.kind == repo_map::SymbolKind::interface_) score += 20;
    if (symbol.path.rfind("include/", 0) == 0) score += 15;
    if (symbol.path.rfind("src/", 0) == 0) score += 10;
    if (symbol.line > 0) score += std::max(0, 10 - symbol.line / 200);
    return score;
}

bool word_boundary_at(std::string_view text, size_t pos, size_t len) {
    if (pos > 0 && is_identifier_char(text[pos - 1])) return false;
    auto end = pos + len;
    return end >= text.size() || !is_identifier_char(text[end]);
}

bool matches_workspace_symbol(const repo_map::RepoMapSymbol& symbol,
                              std::string_view query_lower,
                              std::string_view kind,
                              std::string_view language) {
    if (!kind.empty() && repo_map::to_string(symbol.kind) != kind) return false;
    if (!language.empty() && symbol.language != language) return false;
    if (query_lower.empty()) return true;
    return lower_copy(symbol.name).find(query_lower) != std::string::npos;
}

int workspace_symbol_score(const repo_map::RepoMapSymbol& symbol, std::string_view query_lower) {
    int score = 0;
    auto name_lower = lower_copy(symbol.name);
    if (!query_lower.empty()) {
        if (name_lower == query_lower) score += 80;
        else if (name_lower.rfind(query_lower, 0) == 0) score += 50;
        else if (name_lower.find(query_lower) != std::string::npos) score += 25;
    }
    if (symbol.kind == repo_map::SymbolKind::class_ || symbol.kind == repo_map::SymbolKind::struct_ || symbol.kind == repo_map::SymbolKind::interface_) score += 20;
    if (symbol.path.rfind("include/", 0) == 0) score += 10;
    if (symbol.line > 0) score += std::max(0, 8 - symbol.line / 300);
    return score;
}

repo_map::RepoMapSymbol reference_location(const repo_map::RepoMapFile& file,
                                           const std::string& symbol,
                                           int line,
                                           int column) {
    repo_map::RepoMapSymbol location;
    location.name = symbol;
    location.kind = repo_map::SymbolKind::unknown;
    location.path = file.path;
    location.line = line;
    location.column = column;
    location.language = file.language;
    return location;
}

} // namespace

CodeIntelLocation location_from_symbol(const repo_map::RepoMapSymbol& symbol, std::string preview, int end_column, int score) {
    CodeIntelLocation location;
    location.path = symbol.path;
    location.line = symbol.line;
    location.column = symbol.column;
    location.end_column = end_column > 0 ? end_column : symbol.column + static_cast<int>(symbol.name.size());
    location.symbol = symbol.name;
    location.kind = repo_map::to_string(symbol.kind);
    location.signature = symbol.signature;
    location.container = symbol.container;
    location.language = symbol.language;
    location.preview = std::move(preview);
    location.score = score;
    return location;
}

Json location_json(const CodeIntelLocation& location) {
    Json json{{"path", location.path},
              {"line", location.line},
              {"column", location.column},
              {"end_column", location.end_column},
              {"symbol", location.symbol},
              {"kind", location.kind},
              {"signature", location.signature},
              {"container", location.container},
              {"language", location.language},
              {"score", location.score}};
    if (!location.preview.empty()) json["preview"] = location.preview;
    return json;
}

Json location_json(const repo_map::RepoMapSymbol& symbol, std::string preview, int end_column, int score) {
    return location_json(location_from_symbol(symbol, std::move(preview), end_column, score));
}

Json to_json(const CodeIntelCapabilitiesResult& result) {
    return Json{{"success", true},
                {"provider", "indexed"},
                {"real_lsp", false},
                {"capabilities", Json{{"document_symbols", result.document_symbols},
                                       {"definition", result.definition},
                                       {"references", result.references},
                                       {"workspace_symbols", result.workspace_symbols},
                                       {"hover", result.hover},
                                       {"rename", result.rename},
                                       {"code_actions", result.code_actions}}}};
}

Json to_json(const CodeIntelDocumentSymbolsResult& result) {
    Json symbols = Json::array();
    for (const auto& symbol : result.symbols) symbols.push_back(location_json(symbol));
    return Json{{"success", true}, {"provider", "indexed"}, {"real_lsp", false}, {"path", result.path}, {"symbols", symbols}};
}

Json to_json(const CodeIntelWorkspaceSymbolsResult& result) {
    Json symbols = Json::array();
    for (const auto& symbol : result.symbols) symbols.push_back(location_json(symbol));
    return Json{{"success", true},
                {"provider", "indexed"},
                {"real_lsp", false},
                {"query", result.query},
                {"kind", result.kind},
                {"language", result.language},
                {"symbols", symbols},
                {"truncated", result.truncated}};
}

Json to_json(const CodeIntelDefinitionResult& result) {
    Json definitions = Json::array();
    for (const auto& definition : result.definitions) definitions.push_back(location_json(definition));
    return Json{{"success", true},
                {"provider", "indexed"},
                {"real_lsp", false},
                {"symbol", result.symbol},
                {"definitions", definitions},
                {"truncated", result.truncated}};
}

Json to_json(const CodeIntelReferencesResult& result) {
    Json references = Json::array();
    for (const auto& reference : result.references) references.push_back(location_json(reference));
    return Json{{"success", true},
                {"provider", "indexed"},
                {"real_lsp", false},
                {"symbol", result.symbol},
                {"references", references},
                {"scanned_files", result.scanned_files},
                {"truncated", result.truncated}};
}

CodeIntelService::CodeIntelService(workspace::WorkspaceContext ws_ctx,
                                   std::shared_ptr<repo_map::RepoMapService> repo_map_service)
    : ws_ctx_(std::move(ws_ctx)), repo_map_service_(std::move(repo_map_service)) {
    if (!repo_map_service_) repo_map_service_ = std::make_shared<repo_map::RepoMapService>(ws_ctx_);
}

repo_map::RepoMapService::Options CodeIntelService::repo_map_options(const CodeIntelOptions& options) const {
    repo_map::RepoMapService::Options repo_options;
    repo_options.max_files = options.max_files;
    repo_options.max_symbols = options.max_symbols;
    repo_options.max_dependencies = 0;
    repo_options.max_file_bytes = options.max_file_bytes;
    repo_options.include_external = options.include_external;
    repo_options.include_hidden = options.include_hidden;
    return repo_options;
}

std::filesystem::path CodeIntelService::project_root() const {
    if (!ws_ctx_.project_path.empty()) return std::filesystem::path(to_std(ws_ctx_.project_path));
    std::error_code ec;
    auto cwd = std::filesystem::current_path(ec);
    return ec ? std::filesystem::path() : cwd;
}

bool CodeIntelService::validate_relative_path(std::string_view input, std::string& normalized, std::string& error) const {
    if (input.empty()) {
        error = "missing path";
        return false;
    }
    std::filesystem::path rel{std::string(input)};
    if (rel.is_absolute()) {
        error = "absolute path is not allowed";
        return false;
    }
    auto normal = rel.lexically_normal().generic_string();
    if (normal.empty() || normal == "." || normal == ".." || normal.rfind("../", 0) == 0 || normal.find("/../") != std::string::npos) {
        error = "path escapes workspace";
        return false;
    }
    std::error_code ec;
    auto root = std::filesystem::weakly_canonical(project_root(), ec);
    if (ec) {
        error = "workspace root unavailable";
        return false;
    }
    auto target = std::filesystem::weakly_canonical(root / normal, ec);
    if (ec) target = std::filesystem::weakly_canonical((root / normal).parent_path(), ec) / (root / normal).filename();
    auto root_text = root.string();
    auto target_text = target.string();
    if (target_text != root_text && target_text.rfind(root_text + std::string(1, std::filesystem::path::preferred_separator), 0) != 0) {
        error = "path escapes workspace";
        return false;
    }
    normalized = normal;
    return true;
}

std::string CodeIntelService::symbol_from_query(const CodeIntelQuery& query, std::string& error) const {
    auto direct = trim(query.symbol);
    if (!direct.empty()) {
        if (!valid_symbol_name(direct)) error = "invalid symbol";
        return error.empty() ? direct : std::string();
    }
    std::string normalized;
    if (!validate_relative_path(query.path, normalized, error)) return {};
    if (query.line <= 0 || query.column <= 0) {
        error = "line and column must be positive";
        return {};
    }
    std::string read_error;
    auto content = read_file(project_root() / normalized, read_error);
    if (!read_error.empty()) {
        error = read_error;
        return {};
    }
    auto token = token_at_position(split_lines(content), query.line, query.column);
    if (token.empty()) error = "no symbol at location";
    return token;
}

domain::AppResult<CodeIntelCapabilitiesResult> CodeIntelService::capabilities() const {
    return domain::AppResult<CodeIntelCapabilitiesResult>::success(CodeIntelCapabilitiesResult{});
}

workspace_index::RequestIndexSession CodeIntelService::request_session() const {
    return repo_map_service_->request_session();
}

domain::AppResult<CodeIntelDocumentSymbolsResult> CodeIntelService::document_symbols(std::string_view path) const {
    auto session = request_session();
    return document_symbols(path, session);
}

domain::AppResult<CodeIntelDocumentSymbolsResult> CodeIntelService::document_symbols(std::string_view path,
                                                                                     workspace_index::RequestIndexSession& request_session) const {
    std::string normalized;
    std::string error;
    if (!validate_relative_path(path, normalized, error)) return query_error<CodeIntelDocumentSymbolsResult>("path_outside_workspace", error);
    CodeIntelOptions options;
    auto index = repo_map_service_->snapshot(repo_map_options(options), request_session);
    if (!index.success) return repo_map_index_error<CodeIntelDocumentSymbolsResult>(index);
    CodeIntelDocumentSymbolsResult result;
    result.path = normalized;
    auto root = project_root();
    for (const auto& symbol : index.symbols) {
        if (symbol.path != normalized) continue;
        result.symbols.push_back(location_from_symbol(symbol, preview_for_line(root, symbol.path, symbol.line, options.max_file_bytes), 0, 0));
    }
    return domain::AppResult<CodeIntelDocumentSymbolsResult>::success(std::move(result));
}

domain::AppResult<CodeIntelWorkspaceSymbolsResult> CodeIntelService::workspace_symbols(std::string_view query,
                                                                                       std::string_view kind,
                                                                                       std::string_view language,
                                                                                       int limit,
                                                                                       const CodeIntelOptions& options) const {
    auto session = request_session();
    return workspace_symbols(query, kind, language, limit, options, session);
}

domain::AppResult<CodeIntelWorkspaceSymbolsResult> CodeIntelService::workspace_symbols(std::string_view query,
                                                                                       std::string_view kind,
                                                                                       std::string_view language,
                                                                                       int limit,
                                                                                       const CodeIntelOptions& options,
                                                                                       workspace_index::RequestIndexSession& request_session) const {
    auto query_text = trim(std::string(query));
    auto kind_text = trim(std::string(kind));
    auto language_text = trim(std::string(language));
    auto query_lower = lower_copy(query_text);
    auto capped_limit = std::clamp(limit > 0 ? limit : 50, 1, 200);
    auto index = repo_map_service_->snapshot(repo_map_options(options), request_session);
    if (!index.success) return repo_map_index_error<CodeIntelWorkspaceSymbolsResult>(index);

    std::vector<std::pair<repo_map::RepoMapSymbol, int>> matches;
    for (const auto& symbol : index.symbols) {
        if (!matches_workspace_symbol(symbol, query_lower, kind_text, language_text)) continue;
        matches.emplace_back(symbol, workspace_symbol_score(symbol, query_lower));
    }
    std::sort(matches.begin(), matches.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second > b.second;
        if (a.first.name != b.first.name) return a.first.name < b.first.name;
        if (a.first.path != b.first.path) return a.first.path < b.first.path;
        return a.first.line < b.first.line;
    });

    CodeIntelWorkspaceSymbolsResult result;
    result.query = query_text;
    result.kind = kind_text;
    result.language = language_text;
    auto root = project_root();
    for (const auto& [symbol, score] : matches) {
        if (static_cast<int>(result.symbols.size()) >= capped_limit) {
            result.truncated = true;
            break;
        }
        result.symbols.push_back(location_from_symbol(symbol, preview_for_line(root, symbol.path, symbol.line, options.max_file_bytes), 0, score));
    }
    return domain::AppResult<CodeIntelWorkspaceSymbolsResult>::success(std::move(result));
}

domain::AppResult<CodeIntelDefinitionResult> CodeIntelService::definition(const CodeIntelQuery& query, const CodeIntelOptions& options) const {
    auto session = request_session();
    return definition(query, options, session);
}

domain::AppResult<CodeIntelDefinitionResult> CodeIntelService::definition(const CodeIntelQuery& query,
                                                                          const CodeIntelOptions& options,
                                                                          workspace_index::RequestIndexSession& request_session) const {
    std::string error;
    auto symbol_name = symbol_from_query(query, error);
    if (!error.empty()) return query_error<CodeIntelDefinitionResult>(error == "path escapes workspace" ? "path_outside_workspace" : "invalid_query", error);
    auto index = repo_map_service_->snapshot(repo_map_options(options), request_session);
    if (!index.success) return repo_map_index_error<CodeIntelDefinitionResult>(index);
    std::vector<std::pair<repo_map::RepoMapSymbol, int>> matches;
    for (const auto& symbol : index.symbols) {
        if (symbol.name == symbol_name) matches.emplace_back(symbol, symbol_score(symbol, query));
    }
    std::sort(matches.begin(), matches.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second > b.second;
        if (a.first.path != b.first.path) return a.first.path < b.first.path;
        return a.first.line < b.first.line;
    });
    auto limit = std::clamp(query.limit > 0 ? query.limit : 50, 1, 200);
    CodeIntelDefinitionResult result;
    result.symbol = symbol_name;
    auto root = project_root();
    for (const auto& [symbol, score] : matches) {
        if (static_cast<int>(result.definitions.size()) >= limit) {
            result.truncated = true;
            break;
        }
        result.definitions.push_back(location_from_symbol(symbol, preview_for_line(root, symbol.path, symbol.line, options.max_file_bytes), 0, score));
    }
    return domain::AppResult<CodeIntelDefinitionResult>::success(std::move(result));
}

domain::AppResult<CodeIntelReferencesResult> CodeIntelService::references(const CodeIntelQuery& query, const CodeIntelOptions& options) const {
    auto session = request_session();
    return references(query, options, session);
}

domain::AppResult<CodeIntelReferencesResult> CodeIntelService::references(const CodeIntelQuery& query,
                                                                          const CodeIntelOptions& options,
                                                                          workspace_index::RequestIndexSession& request_session) const {
    std::string error;
    auto symbol_name = symbol_from_query(query, error);
    if (!error.empty()) return query_error<CodeIntelReferencesResult>(error == "path escapes workspace" ? "path_outside_workspace" : "invalid_query", error);
    auto index = repo_map_service_->snapshot(repo_map_options(options), request_session);
    if (!index.success) return repo_map_index_error<CodeIntelReferencesResult>(index);
    auto root = project_root();
    auto limit = std::clamp(query.limit > 0 ? query.limit : options.max_references, 1, 200);
    CodeIntelReferencesResult result;
    result.symbol = symbol_name;
    for (const auto& file : index.files) {
        if (file.skipped || file.kind == repo_map::FileKind::external || file.size_bytes > options.max_file_bytes) continue;
        if (static_cast<int>(result.references.size()) >= limit) {
            result.truncated = true;
            break;
        }
        ++result.scanned_files;
        std::string read_error;
        auto content = read_file(root / file.path, read_error, options.max_file_bytes);
        if (!read_error.empty()) continue;
        auto lines = split_lines(content);
        for (size_t i = 0; i < lines.size(); ++i) {
            const auto& line = lines[i];
            size_t pos = 0;
            while ((pos = line.find(symbol_name, pos)) != std::string::npos) {
                if (word_boundary_at(line, pos, symbol_name.size())) {
                    auto location = reference_location(file, symbol_name, static_cast<int>(i + 1), static_cast<int>(pos + 1));
                    result.references.push_back(location_from_symbol(location, trim(line), static_cast<int>(pos + symbol_name.size() + 1), 0));
                    if (static_cast<int>(result.references.size()) >= limit) {
                        result.truncated = true;
                        break;
                    }
                }
                pos += symbol_name.size();
            }
            if (result.truncated) break;
        }
        if (result.truncated) break;
    }
    return domain::AppResult<CodeIntelReferencesResult>::success(std::move(result));
}

} // namespace ben_gear::code_intel
