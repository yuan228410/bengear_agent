#include "ben_gear/repo_map/repo_map_service.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace ben_gear::repo_map {

namespace {

std::string to_std(const base::container::String& value) {
    return std::string(value.data(), value.size());
}

Json error_json(std::string_view type, std::string_view message) {
    return Json{{"success", false}, {"error_type", std::string(type)}, {"message", std::string(message)}};
}

std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string trim(std::string value) {
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
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
    return lines;
}

bool contains_text(std::string_view text, std::string_view query) {
    if (query.empty()) return true;
    return lower_copy(std::string(text)).find(lower_copy(std::string(query))) != std::string::npos;
}

bool has_prefix(std::string_view text, std::string_view prefix) {
    return text.rfind(prefix, 0) == 0;
}

std::string extension_lower(const std::filesystem::path& path) {
    return lower_copy(path.extension().string());
}

std::string filename_lower(const std::filesystem::path& path) {
    return lower_copy(path.filename().string());
}

bool is_hidden_path(const std::filesystem::path& relative) {
    for (const auto& part : relative) {
        auto text = part.string();
        if (!text.empty() && text[0] == '.') return true;
    }
    return false;
}

bool wildcard_match(std::string_view pattern, std::string_view text) {
    if (pattern.empty()) return false;
    if (pattern.back() == '*') return has_prefix(text, pattern.substr(0, pattern.size() - 1));
    return pattern == text;
}

bool is_excluded_path(const std::filesystem::path& relative) {
    static const std::unordered_set<std::string> exact = {
        ".git", ".claude", "node_modules", ".venv", "venv", "dist", "out", "target",
        "third_party", "vendor", ".cache", ".DS_Store", "__pycache__"
    };
    static const std::vector<std::string> patterns = {"build", "build_*", "cmake-build-*"};
    for (const auto& part : relative) {
        auto text = part.string();
        if (exact.count(text)) return true;
        for (const auto& pattern : patterns) {
            if (wildcard_match(pattern, text)) return true;
        }
    }
    return false;
}

bool looks_binary(const std::string& content) {
    auto scan = std::min<size_t>(content.size(), 4096);
    for (size_t i = 0; i < scan; ++i) {
        if (content[i] == '\0') return true;
    }
    return false;
}

std::string read_file(const std::filesystem::path& path, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "failed to read file";
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::string language_for_path(const std::filesystem::path& path) {
    auto name = filename_lower(path);
    auto ext = extension_lower(path);
    if (name == "cmakelists.txt") return "cmake";
    if (name == "makefile") return "make";
    if (name == "package.json") return "json";
    if (ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".c") return "cpp";
    if (ext == ".hpp" || ext == ".hh" || ext == ".hxx" || ext == ".h") return "cpp";
    if (ext == ".ts" || ext == ".tsx") return "typescript";
    if (ext == ".js" || ext == ".jsx") return "javascript";
    if (ext == ".vue") return "vue";
    if (ext == ".py") return "python";
    if (ext == ".go") return "go";
    if (ext == ".rs") return "rust";
    if (ext == ".md" || ext == ".rst") return "markdown";
    if (ext == ".json") return "json";
    if (ext == ".yaml" || ext == ".yml") return "yaml";
    if (ext == ".toml") return "toml";
    if (ext == ".ini" || ext == ".cfg") return "config";
    return "unknown";
}

bool is_test_path(const std::string& path) {
    auto lower = lower_copy(path);
    auto name = lower_copy(std::filesystem::path(path).filename().string());
    return lower.find("/test/") != std::string::npos || lower.find("/tests/") != std::string::npos ||
           has_prefix(name, "test_") || name.find("_test.") != std::string::npos ||
           name.find(".test.") != std::string::npos || name.find(".spec.") != std::string::npos;
}

FileKind kind_for_path(const std::filesystem::path& path, const std::string& language) {
    auto rel = path.generic_string();
    auto name = filename_lower(path);
    auto ext = extension_lower(path);
    if (is_excluded_path(path)) return FileKind::external;
    if (is_test_path(rel)) return FileKind::test;
    if (name == "cmakelists.txt" || ext == ".mk" || ext == ".cmake") return FileKind::build;
    if (language == "markdown") return FileKind::document;
    if (language == "json" || language == "yaml" || language == "toml" || language == "config") return FileKind::config;
    if (ext == ".hpp" || ext == ".hh" || ext == ".hxx" || ext == ".h") return FileKind::header;
    if (language == "cpp" || language == "typescript" || language == "javascript" || language == "vue" ||
        language == "python" || language == "go" || language == "rust") return FileKind::source;
    return FileKind::unknown;
}

int score_file(const RepoMapFile& file) {
    int score = 0;
    if (file.changed) score += 50;
    if (file.recent) score += 20;
    if (has_prefix(file.path, "include/")) score += 20;
    if (has_prefix(file.path, "src/")) score += 20;
    if (file.kind == FileKind::test) score += 15;
    if (file.kind == FileKind::build || file.kind == FileKind::config) score += 10;
    if (file.kind == FileKind::external || file.kind == FileKind::generated) score -= 30;
    if (file.size_bytes > 512 * 1024) score -= 10;
    return score;
}

void increment(Json& object, const std::string& key) {
    object[key] = object.value(key, 0) + 1;
}

RepoMapSymbol make_symbol(std::string name, SymbolKind kind, const RepoMapFile& file, int line, std::string signature = {}, std::string container = {}) {
    RepoMapSymbol symbol;
    symbol.name = std::move(name);
    symbol.kind = kind;
    symbol.path = file.path;
    symbol.line = line;
    symbol.column = 1;
    symbol.signature = std::move(signature);
    symbol.container = std::move(container);
    symbol.language = file.language;
    return symbol;
}

std::vector<RepoMapSymbol> extract_symbols(const RepoMapFile& file, const std::vector<std::string>& lines, int max_symbols) {
    std::vector<RepoMapSymbol> symbols;
    std::string current_container;
    auto push = [&](RepoMapSymbol symbol) {
        if (static_cast<int>(symbols.size()) < max_symbols) symbols.push_back(std::move(symbol));
    };
    std::regex cpp_class(R"(^\s*(class|struct|enum(?:\s+class)?)\s+([A-Za-z_][A-Za-z0-9_]*))");
    std::regex cpp_ns(R"(^\s*namespace\s+([A-Za-z_][A-Za-z0-9_:]*))");
    std::regex cpp_func(R"(^\s*(?:[A-Za-z_][A-Za-z0-9_:<>~*&\s]+\s+)+([A-Za-z_~][A-Za-z0-9_:~]*)\s*\([^;]*\)\s*(?:const\s*)?(?:\{|$))");
    std::regex py_class(R"(^\s*class\s+([A-Za-z_][A-Za-z0-9_]*))");
    std::regex py_func(R"re(^\s*def\s+([A-Za-z_][A-Za-z0-9_]*)\s*\()re");
    std::regex js_class(R"(^\s*(?:export\s+)?class\s+([A-Za-z_][A-Za-z0-9_]*))");
    std::regex js_func(R"re(^\s*(?:export\s+)?(?:async\s+)?function\s+([A-Za-z_][A-Za-z0-9_]*)\s*\()re");
    std::regex js_arrow(R"(^\s*(?:export\s+)?const\s+([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(?:async\s*)?\(?[^=]*\)?\s*=>)");
    std::regex go_func(R"re(^\s*func\s+(?:\([^)]*\)\s*)?([A-Za-z_][A-Za-z0-9_]*)\s*\()re");
    std::regex go_type(R"(^\s*type\s+([A-Za-z_][A-Za-z0-9_]*)\s+(struct|interface))");
    std::regex rust_item(R"(^\s*(?:pub\s+)?(struct|enum|trait|fn)\s+([A-Za-z_][A-Za-z0-9_]*))");

    for (size_t i = 0; i < lines.size(); ++i) {
        auto line_no = static_cast<int>(i + 1);
        const auto& line = lines[i];
        std::smatch match;
        if (file.language == "cpp") {
            if (std::regex_search(line, match, cpp_ns)) {
                current_container = match[1];
                push(make_symbol(match[1], SymbolKind::namespace_, file, line_no, trim(line)));
            } else if (std::regex_search(line, match, cpp_class)) {
                auto keyword = std::string(match[1]);
                auto kind = keyword.rfind("class", 0) == 0 ? SymbolKind::class_ : keyword == "struct" ? SymbolKind::struct_ : SymbolKind::enum_;
                push(make_symbol(match[2], kind, file, line_no, trim(line), current_container));
            } else if (std::regex_search(line, match, cpp_func)) {
                auto name = std::string(match[1]);
                auto lower = lower_copy(name);
                if (lower != "if" && lower != "for" && lower != "while" && lower != "switch" && lower != "catch") {
                    push(make_symbol(name, SymbolKind::function, file, line_no, trim(line), current_container));
                }
            }
        } else if (file.language == "python") {
            if (std::regex_search(line, match, py_class)) push(make_symbol(match[1], SymbolKind::class_, file, line_no, trim(line)));
            else if (std::regex_search(line, match, py_func)) push(make_symbol(match[1], SymbolKind::function, file, line_no, trim(line)));
        } else if (file.language == "typescript" || file.language == "javascript" || file.language == "vue") {
            if (std::regex_search(line, match, js_class)) push(make_symbol(match[1], SymbolKind::class_, file, line_no, trim(line)));
            else if (std::regex_search(line, match, js_func)) push(make_symbol(match[1], SymbolKind::function, file, line_no, trim(line)));
            else if (std::regex_search(line, match, js_arrow)) push(make_symbol(match[1], SymbolKind::function, file, line_no, trim(line)));
        } else if (file.language == "go") {
            if (std::regex_search(line, match, go_func)) push(make_symbol(match[1], SymbolKind::function, file, line_no, trim(line)));
            else if (std::regex_search(line, match, go_type)) push(make_symbol(match[1], std::string(match[2]) == "struct" ? SymbolKind::struct_ : SymbolKind::interface_, file, line_no, trim(line)));
        } else if (file.language == "rust") {
            if (std::regex_search(line, match, rust_item)) {
                auto keyword = std::string(match[1]);
                auto kind = keyword == "struct" ? SymbolKind::struct_ : keyword == "enum" ? SymbolKind::enum_ : keyword == "trait" ? SymbolKind::interface_ : SymbolKind::function;
                push(make_symbol(match[2], kind, file, line_no, trim(line)));
            }
        }
    }
    return symbols;
}

RepoMapDependency make_dependency(const RepoMapFile& file, std::string target, DependencyKind kind, int line) {
    RepoMapDependency dep;
    dep.from = file.path;
    dep.target = std::move(target);
    dep.kind = kind;
    dep.line = line;
    return dep;
}

std::vector<RepoMapDependency> extract_dependencies(const RepoMapFile& file, const std::vector<std::string>& lines, int max_dependencies) {
    std::vector<RepoMapDependency> deps;
    auto push = [&](RepoMapDependency dep) {
        if (static_cast<int>(deps.size()) < max_dependencies) deps.push_back(std::move(dep));
    };
    std::regex include_re(R"(^\s*#\s*include\s*[<"]([^>"]+)[>"])");
    std::regex js_import_re(R"(^\s*import\s+.*?\s+from\s+["']([^"']+)["'])");
    std::regex js_import_side_re(R"(^\s*import\s+["']([^"']+)["'])");
    std::regex py_import_re(R"(^\s*import\s+([A-Za-z0-9_\.]+))");
    std::regex py_from_re(R"(^\s*from\s+([A-Za-z0-9_\.]+)\s+import\s+)");
    std::regex rust_use_re(R"(^\s*use\s+([^;]+);)");
    std::regex go_import_re(R"re(^\s*import\s+(?:[A-Za-z_][A-Za-z0-9_]*\s+)?"([^"]+)")re");
    for (size_t i = 0; i < lines.size(); ++i) {
        std::smatch match;
        auto line_no = static_cast<int>(i + 1);
        const auto& line = lines[i];
        if (file.language == "cpp" && std::regex_search(line, match, include_re)) push(make_dependency(file, match[1], DependencyKind::include, line_no));
        else if ((file.language == "typescript" || file.language == "javascript" || file.language == "vue") && std::regex_search(line, match, js_import_re)) push(make_dependency(file, match[1], DependencyKind::import, line_no));
        else if ((file.language == "typescript" || file.language == "javascript" || file.language == "vue") && std::regex_search(line, match, js_import_side_re)) push(make_dependency(file, match[1], DependencyKind::import, line_no));
        else if (file.language == "python" && std::regex_search(line, match, py_import_re)) push(make_dependency(file, match[1], DependencyKind::import, line_no));
        else if (file.language == "python" && std::regex_search(line, match, py_from_re)) push(make_dependency(file, match[1], DependencyKind::import, line_no));
        else if (file.language == "rust" && std::regex_search(line, match, rust_use_re)) push(make_dependency(file, trim(match[1]), DependencyKind::use, line_no));
        else if (file.language == "go" && std::regex_search(line, match, go_import_re)) push(make_dependency(file, match[1], DependencyKind::import, line_no));
    }
    return deps;
}

std::optional<std::string> resolve_dependency(const RepoMapDependency& dep, const std::filesystem::path& root, const std::filesystem::path& from_path) {
    std::vector<std::filesystem::path> candidates;
    auto target = std::filesystem::path(dep.target);
    if (dep.kind == DependencyKind::include) {
        candidates.push_back(from_path.parent_path() / target);
        candidates.push_back(root / target);
        candidates.push_back(root / "include" / target);
    } else if (!dep.target.empty() && (dep.target[0] == '.' || dep.target.find('/') != std::string::npos)) {
        candidates.push_back(from_path.parent_path() / target);
        candidates.push_back(root / target);
        static const char* exts[] = {".ts", ".tsx", ".js", ".jsx", ".py", ".rs", ".go"};
        for (const auto* ext : exts) candidates.push_back(from_path.parent_path() / (dep.target + ext));
    }
    std::error_code ec;
    for (auto candidate : candidates) {
        candidate = candidate.lexically_normal();
        if (std::filesystem::exists(candidate, ec) && !ec) {
            auto rel = std::filesystem::relative(candidate, root, ec);
            if (!ec) return rel.generic_string();
        }
    }
    return std::nullopt;
}

Json top_directories_json(const std::map<std::string, int>& counts) {
    std::vector<std::pair<std::string, int>> items(counts.begin(), counts.end());
    std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second > b.second;
        return a.first < b.first;
    });
    Json result = Json::array();
    for (size_t i = 0; i < items.size() && i < 10; ++i) {
        result.push_back(Json{{"path", items[i].first}, {"files", items[i].second}});
    }
    return result;
}

} // namespace

std::string to_string(FileKind kind) {
    switch (kind) {
        case FileKind::source: return "source";
        case FileKind::header: return "header";
        case FileKind::test: return "test";
        case FileKind::config: return "config";
        case FileKind::document: return "document";
        case FileKind::build: return "build";
        case FileKind::generated: return "generated";
        case FileKind::external: return "external";
        case FileKind::unknown: return "unknown";
    }
    return "unknown";
}

std::string to_string(SymbolKind kind) {
    switch (kind) {
        case SymbolKind::function: return "function";
        case SymbolKind::method: return "method";
        case SymbolKind::class_: return "class";
        case SymbolKind::struct_: return "struct";
        case SymbolKind::enum_: return "enum";
        case SymbolKind::namespace_: return "namespace";
        case SymbolKind::interface_: return "interface";
        case SymbolKind::variable: return "variable";
        case SymbolKind::module: return "module";
        case SymbolKind::unknown: return "unknown";
    }
    return "unknown";
}

std::string to_string(DependencyKind kind) {
    switch (kind) {
        case DependencyKind::include: return "include";
        case DependencyKind::import: return "import";
        case DependencyKind::use: return "use";
        case DependencyKind::require: return "require";
        case DependencyKind::unknown: return "unknown";
    }
    return "unknown";
}

Json to_json(const RepoMapFile& file) {
    return Json{{"path", file.path},
                {"language", file.language},
                {"kind", to_string(file.kind)},
                {"size_bytes", file.size_bytes},
                {"line_count", file.line_count},
                {"skipped", file.skipped},
                {"skip_reason", file.skip_reason},
                {"changed", file.changed},
                {"recent", file.recent},
                {"score", file.score}};
}

Json to_json(const RepoMapSymbol& symbol) {
    return Json{{"name", symbol.name},
                {"kind", to_string(symbol.kind)},
                {"path", symbol.path},
                {"line", symbol.line},
                {"column", symbol.column},
                {"signature", symbol.signature},
                {"container", symbol.container},
                {"language", symbol.language}};
}

Json to_json(const RepoMapDependency& dependency) {
    return Json{{"from", dependency.from},
                {"target", dependency.target},
                {"kind", to_string(dependency.kind)},
                {"line", dependency.line},
                {"resolved", dependency.resolved},
                {"resolved_path", dependency.resolved_path}};
}

Json to_json(const RepoMapSummary& summary) {
    return Json{{"project_root", summary.project_root},
                {"total_files", summary.total_files},
                {"indexed_files", summary.indexed_files},
                {"skipped_files", summary.skipped_files},
                {"total_symbols", summary.total_symbols},
                {"truncated", summary.truncated},
                {"languages", summary.languages},
                {"file_kinds", summary.file_kinds},
                {"top_directories", summary.top_directories},
                {"changed_files", summary.changed_files},
                {"recent_files", summary.recent_files},
                {"test_suggestions", summary.test_suggestions}};
}

Json to_json(const RepoMapIndex& index) {
    Json files = Json::array();
    Json symbols = Json::array();
    Json dependencies = Json::array();
    for (const auto& file : index.files) files.push_back(to_json(file));
    for (const auto& symbol : index.symbols) symbols.push_back(to_json(symbol));
    for (const auto& dependency : index.dependencies) dependencies.push_back(to_json(dependency));
    return Json{{"success", index.success},
                {"error_type", index.error_type},
                {"message", index.message},
                {"summary", to_json(index.summary)},
                {"files", files},
                {"symbols", symbols},
                {"dependencies", dependencies}};
}

RepoMapService::RepoMapService(workspace::WorkspaceContext ws_ctx,
                               std::shared_ptr<git::GitService> git_service,
                               std::shared_ptr<test_loop::TestLoopService> test_loop_service,
                               std::shared_ptr<workspace_index::WorkspaceIndexService> index_service)
    : ws_ctx_(std::move(ws_ctx)),
      git_service_(std::move(git_service)),
      test_loop_service_(std::move(test_loop_service)),
      index_service_(std::move(index_service)) {
    if (!index_service_) index_service_ = std::make_shared<workspace_index::WorkspaceIndexService>(ws_ctx_);
}

std::filesystem::path RepoMapService::project_root() const {
    if (!ws_ctx_.project_path.empty()) return std::filesystem::path(to_std(ws_ctx_.project_path));
    return std::filesystem::current_path();
}

bool RepoMapService::validate_relative_path(const std::string& input, std::string& normalized, std::string& error) const {
    if (input.empty()) {
        error = "path must be non-empty";
        return false;
    }
    std::filesystem::path path(input);
    if (path.is_absolute()) {
        error = "repo map paths must be relative to the workspace";
        return false;
    }
    path = path.lexically_normal();
    auto generic = path.generic_string();
    if (generic == "." || generic == ".." || generic.rfind("../", 0) == 0 || generic.find("/../") != std::string::npos) {
        error = "repo map path escapes workspace";
        return false;
    }
    normalized = generic;
    return true;
}

workspace_index::WorkspaceIndexOptions RepoMapService::index_options(const Options& options) const {
    workspace_index::WorkspaceIndexOptions mapped;
    mapped.max_files = options.max_files;
    mapped.max_symbols = options.max_symbols;
    mapped.max_dependencies = options.max_dependencies;
    mapped.max_file_bytes = options.max_file_bytes;
    mapped.include_external = options.include_external;
    mapped.include_hidden = options.include_hidden;
    mapped.refresh = options.refresh;
    return mapped;
}

RepoMapIndex RepoMapService::build_index(const Options& options) const {
    if (!index_service_) return scan_index(options);
    return index_service_->snapshot(index_options(options), [this, options]() {
        return scan_index(options);
    });
}

RepoMapIndex RepoMapService::scan_index(const Options& options) const {
    RepoMapIndex index;
    index.success = true;
    auto root = project_root();
    std::error_code ec;
    auto canonical_root = std::filesystem::weakly_canonical(root, ec);
    if (ec || !std::filesystem::exists(canonical_root, ec)) {
        index.success = false;
        index.error_type = "workspace_not_found";
        index.message = ec ? ec.message() : "workspace root does not exist";
        return index;
    }
    index.summary.project_root = canonical_root.string();

    std::set<std::string> changed;
    if (git_service_) {
        auto status = git_service_->status();
        if (status.success) {
            for (const auto& entry : status.entries) changed.insert(std::filesystem::path(entry.path).generic_string());
        }
    }

    if (test_loop_service_) {
        auto inspected = test_loop_service_->inspect();
        if (inspected.ok()) {
            Json suggestions = Json::array();
            for (const auto& suggestion : inspected.value().suggestions) suggestions.push_back(test_loop::to_json(suggestion));
            index.summary.test_suggestions = std::move(suggestions);
        }
    }

    std::map<std::string, int> top_dirs;
    std::vector<RepoMapFile> pending_files;
    for (std::filesystem::recursive_directory_iterator it(canonical_root, std::filesystem::directory_options::skip_permission_denied, ec), end;
         it != end && !ec; it.increment(ec)) {
        const auto& entry = *it;
        auto rel = std::filesystem::relative(entry.path(), canonical_root, ec);
        if (ec) continue;
        if (!options.include_hidden && is_hidden_path(rel)) {
            if (entry.is_directory(ec)) it.disable_recursion_pending();
            continue;
        }
        if (!options.include_external && is_excluded_path(rel)) {
            if (entry.is_directory(ec)) it.disable_recursion_pending();
            continue;
        }
        if (entry.is_symlink(ec)) continue;
        if (!entry.is_regular_file(ec)) continue;

        index.summary.total_files++;
        if (static_cast<int>(pending_files.size()) >= options.max_files) {
            index.summary.truncated = true;
            continue;
        }

        RepoMapFile file;
        file.path = rel.generic_string();
        file.language = language_for_path(rel);
        file.kind = kind_for_path(rel, file.language);
        file.changed = changed.count(file.path) > 0;
        file.size_bytes = static_cast<std::int64_t>(entry.file_size(ec));
        if (ec) file.size_bytes = 0;
        if (file.size_bytes > options.max_file_bytes) {
            file.skipped = true;
            file.skip_reason = "file_too_large";
        }
        file.score = score_file(file);
        pending_files.push_back(std::move(file));
    }

    for (auto& file : pending_files) {
        increment(index.summary.languages, file.language);
        increment(index.summary.file_kinds, to_string(file.kind));
        auto file_path = std::filesystem::path(file.path);
        auto top = file_path.begin();
        if (top != file_path.end()) top_dirs[top->string()]++;
        if (file.changed) index.summary.changed_files.push_back(file.path);
        if (file.skipped) {
            index.summary.skipped_files++;
            index.files.push_back(file);
            continue;
        }

        std::string error;
        auto content = read_file(canonical_root / file.path, error);
        if (!error.empty() || looks_binary(content)) {
            file.skipped = true;
            file.skip_reason = error.empty() ? "binary_file" : error;
            index.summary.skipped_files++;
            index.files.push_back(file);
            continue;
        }
        auto lines = split_lines(content);
        file.line_count = static_cast<int>(lines.size());
        auto symbols = extract_symbols(file, lines, std::max(0, options.max_symbols - static_cast<int>(index.symbols.size())));
        for (auto& symbol : symbols) {
            if (static_cast<int>(index.symbols.size()) >= options.max_symbols) {
                index.summary.truncated = true;
                break;
            }
            index.symbols.push_back(std::move(symbol));
        }
        auto deps = extract_dependencies(file, lines, std::max(0, options.max_dependencies - static_cast<int>(index.dependencies.size())));
        for (auto& dep : deps) {
            if (static_cast<int>(index.dependencies.size()) >= options.max_dependencies) {
                index.summary.truncated = true;
                break;
            }
            auto resolved = resolve_dependency(dep, canonical_root, canonical_root / file.path);
            if (resolved) {
                dep.resolved = true;
                dep.resolved_path = *resolved;
            }
            index.dependencies.push_back(std::move(dep));
        }
        file.score = score_file(file);
        index.summary.indexed_files++;
        index.files.push_back(std::move(file));
    }

    std::sort(index.files.begin(), index.files.end(), [](const auto& a, const auto& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.path < b.path;
    });
    std::sort(index.symbols.begin(), index.symbols.end(), [](const auto& a, const auto& b) {
        if (a.path != b.path) return a.path < b.path;
        if (a.line != b.line) return a.line < b.line;
        return a.name < b.name;
    });
    std::sort(index.dependencies.begin(), index.dependencies.end(), [](const auto& a, const auto& b) {
        if (a.from != b.from) return a.from < b.from;
        if (a.line != b.line) return a.line < b.line;
        return a.target < b.target;
    });
    index.summary.total_symbols = static_cast<int>(index.symbols.size());
    index.summary.top_directories = top_directories_json(top_dirs);
    return index;
}

RepoMapIndex RepoMapService::snapshot() const {
    return snapshot(Options{});
}

RepoMapIndex RepoMapService::snapshot(const Options& options) const {
    return build_index(options);
}

Json RepoMapService::overview() const {
    return overview(Options{});
}

Json RepoMapService::overview(const Options& options) const {
    auto index = build_index(options);
    if (!index.success) return to_json(index);
    Json important_files = Json::array();
    Json important_symbols = Json::array();
    for (size_t i = 0; i < index.files.size() && i < 30; ++i) important_files.push_back(to_json(index.files[i]));
    for (size_t i = 0; i < index.symbols.size() && i < 50; ++i) important_symbols.push_back(to_json(index.symbols[i]));
    return Json{{"success", true},
                {"summary", to_json(index.summary)},
                {"important_files", important_files},
                {"important_symbols", important_symbols}};
}

Json RepoMapService::find_files(const std::string& query, const std::string& kind, const std::string& language, int limit) const {
    return find_files(query, kind, language, limit, Options{});
}

Json RepoMapService::find_files(const std::string& query, const std::string& kind, const std::string& language, int limit, const Options& options) const {
    auto index = build_index(options);
    if (!index.success) return to_json(index);
    Json files = Json::array();
    limit = std::clamp(limit, 1, 200);
    for (const auto& file : index.files) {
        if (!contains_text(file.path, query)) continue;
        if (!kind.empty() && to_string(file.kind) != kind) continue;
        if (!language.empty() && file.language != language) continue;
        files.push_back(to_json(file));
        if (static_cast<int>(files.size()) >= limit) break;
    }
    return Json{{"success", true}, {"files", files}, {"summary", to_json(index.summary)}};
}

Json RepoMapService::find_symbols(const std::string& query, const std::string& kind, const std::string& language, int limit) const {
    return find_symbols(query, kind, language, limit, Options{});
}

Json RepoMapService::find_symbols(const std::string& query, const std::string& kind, const std::string& language, int limit, const Options& options) const {
    auto index = build_index(options);
    if (!index.success) return to_json(index);
    Json symbols = Json::array();
    limit = std::clamp(limit, 1, 200);
    for (const auto& symbol : index.symbols) {
        if (!contains_text(symbol.name, query)) continue;
        if (!kind.empty() && to_string(symbol.kind) != kind) continue;
        if (!language.empty() && symbol.language != language) continue;
        symbols.push_back(to_json(symbol));
        if (static_cast<int>(symbols.size()) >= limit) break;
    }
    return Json{{"success", true}, {"symbols", symbols}, {"summary", to_json(index.summary)}};
}

Json RepoMapService::explain_path(const std::string& path) const {
    return explain_path(path, Options{});
}

Json RepoMapService::explain_path(const std::string& path, const Options& options) const {
    std::string normalized;
    std::string error;
    if (!validate_relative_path(path, normalized, error)) return error_json("path_outside_workspace", error);
    auto index = build_index(options);
    if (!index.success) return to_json(index);
    const RepoMapFile* found = nullptr;
    for (const auto& file : index.files) {
        if (file.path == normalized) {
            found = &file;
            break;
        }
    }
    if (!found) return error_json("path_not_found", "path is not indexed");
    Json symbols = Json::array();
    Json dependencies = Json::array();
    Json dependents = Json::array();
    Json related_tests = Json::array();
    auto stem = lower_copy(std::filesystem::path(normalized).stem().string());
    for (const auto& symbol : index.symbols) if (symbol.path == normalized) symbols.push_back(to_json(symbol));
    for (const auto& dep : index.dependencies) {
        if (dep.from == normalized) dependencies.push_back(to_json(dep));
        if (dep.resolved_path == normalized) dependents.push_back(to_json(dep));
    }
    for (const auto& file : index.files) {
        if (file.kind == FileKind::test && contains_text(file.path, stem)) related_tests.push_back(to_json(file));
    }
    return Json{{"success", true},
                {"file", to_json(*found)},
                {"symbols", symbols},
                {"dependencies", dependencies},
                {"dependents", dependents},
                {"related_tests", related_tests},
                {"summary", to_json(index.summary)}};
}

} // namespace ben_gear::repo_map
