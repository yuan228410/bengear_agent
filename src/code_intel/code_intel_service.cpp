#include "ben_gear/code_intel/code_intel_service.hpp"

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

Json error_json(std::string_view type, std::string_view message) {
    return Json{{"success", false}, {"error_type", std::string(type)}, {"message", std::string(message)}, {"provider", "indexed"}, {"real_lsp", false}};
}

std::string trim(std::string value) {
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
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

Json location_json(const repo_map::RepoMapSymbol& symbol, std::string preview, int end_column, int score) {
    Json json{{"path", symbol.path},
              {"line", symbol.line},
              {"column", symbol.column},
              {"end_column", end_column > 0 ? end_column : symbol.column + static_cast<int>(symbol.name.size())},
              {"symbol", symbol.name},
              {"kind", repo_map::to_string(symbol.kind)},
              {"signature", symbol.signature},
              {"container", symbol.container},
              {"language", symbol.language},
              {"score", score}};
    if (!preview.empty()) json["preview"] = std::move(preview);
    return json;
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

Json CodeIntelService::capabilities() const {
    return Json{{"success", true},
                {"provider", "indexed"},
                {"real_lsp", false},
                {"capabilities", Json{{"document_symbols", true},
                                       {"definition", true},
                                       {"references", true},
                                       {"hover", false},
                                       {"rename", false},
                                       {"code_actions", false}}}};
}

Json CodeIntelService::document_symbols(std::string_view path) const {
    std::string normalized;
    std::string error;
    if (!validate_relative_path(path, normalized, error)) return error_json("path_outside_workspace", error);
    CodeIntelOptions options;
    auto index = repo_map_service_->snapshot(repo_map_options(options));
    if (!index.success) return repo_map::to_json(index);
    Json symbols = Json::array();
    auto root = project_root();
    for (const auto& symbol : index.symbols) {
        if (symbol.path != normalized) continue;
        symbols.push_back(location_json(symbol, preview_for_line(root, symbol.path, symbol.line, options.max_file_bytes), 0, 0));
    }
    return Json{{"success", true}, {"provider", "indexed"}, {"real_lsp", false}, {"path", normalized}, {"symbols", symbols}};
}

Json CodeIntelService::definition(const CodeIntelQuery& query, const CodeIntelOptions& options) const {
    std::string error;
    auto symbol_name = symbol_from_query(query, error);
    if (!error.empty()) return error_json(error == "path escapes workspace" ? "path_outside_workspace" : "invalid_query", error);
    auto index = repo_map_service_->snapshot(repo_map_options(options));
    if (!index.success) return repo_map::to_json(index);
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
    Json definitions = Json::array();
    auto root = project_root();
    bool truncated = false;
    for (const auto& [symbol, score] : matches) {
        if (static_cast<int>(definitions.size()) >= limit) {
            truncated = true;
            break;
        }
        definitions.push_back(location_json(symbol, preview_for_line(root, symbol.path, symbol.line, options.max_file_bytes), 0, score));
    }
    return Json{{"success", true},
                {"provider", "indexed"},
                {"real_lsp", false},
                {"symbol", symbol_name},
                {"definitions", definitions},
                {"truncated", truncated}};
}

Json CodeIntelService::references(const CodeIntelQuery& query, const CodeIntelOptions& options) const {
    std::string error;
    auto symbol_name = symbol_from_query(query, error);
    if (!error.empty()) return error_json(error == "path escapes workspace" ? "path_outside_workspace" : "invalid_query", error);
    auto index = repo_map_service_->snapshot(repo_map_options(options));
    if (!index.success) return repo_map::to_json(index);
    auto root = project_root();
    auto limit = std::clamp(query.limit > 0 ? query.limit : options.max_references, 1, 200);
    Json refs = Json::array();
    bool truncated = false;
    int scanned_files = 0;
    for (const auto& file : index.files) {
        if (file.skipped || file.kind == repo_map::FileKind::external || file.size_bytes > options.max_file_bytes) continue;
        if (static_cast<int>(refs.size()) >= limit) {
            truncated = true;
            break;
        }
        ++scanned_files;
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
                    refs.push_back(location_json(location, trim(line), static_cast<int>(pos + symbol_name.size() + 1), 0));
                    if (static_cast<int>(refs.size()) >= limit) {
                        truncated = true;
                        break;
                    }
                }
                pos += symbol_name.size();
            }
            if (truncated) break;
        }
        if (truncated) break;
    }
    return Json{{"success", true},
                {"provider", "indexed"},
                {"real_lsp", false},
                {"symbol", symbol_name},
                {"references", refs},
                {"scanned_files", scanned_files},
                {"truncated", truncated}};
}

} // namespace ben_gear::code_intel
