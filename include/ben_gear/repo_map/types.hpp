#pragma once

#include "ben_gear/base/utils/json.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace ben_gear::repo_map {

enum class FileKind {
    source,
    header,
    test,
    config,
    document,
    build,
    generated,
    external,
    unknown,
};

enum class SymbolKind {
    function,
    method,
    class_,
    struct_,
    enum_,
    namespace_,
    interface_,
    variable,
    module,
    unknown,
};

enum class DependencyKind {
    include,
    import,
    use,
    require,
    unknown,
};

struct RepoMapFile {
    std::string path;
    std::string language;
    FileKind kind = FileKind::unknown;
    std::int64_t size_bytes = 0;
    int line_count = 0;
    bool skipped = false;
    std::string skip_reason;
    bool changed = false;
    bool recent = false;
    int score = 0;
};

struct RepoMapSymbol {
    std::string name;
    SymbolKind kind = SymbolKind::unknown;
    std::string path;
    int line = 0;
    int column = 0;
    std::string signature;
    std::string container;
    std::string language;
};

struct RepoMapDependency {
    std::string from;
    std::string target;
    DependencyKind kind = DependencyKind::unknown;
    int line = 0;
    bool resolved = false;
    std::string resolved_path;
};

struct RepoMapSummary {
    std::string project_root;
    int total_files = 0;
    int indexed_files = 0;
    int skipped_files = 0;
    int total_symbols = 0;
    bool truncated = false;
    Json languages = Json::object();
    Json file_kinds = Json::object();
    Json top_directories = Json::array();
    Json changed_files = Json::array();
    Json recent_files = Json::array();
    Json test_suggestions = Json::array();
};

struct RepoMapIndex {
    bool success = false;
    std::string error_type;
    std::string message;
    RepoMapSummary summary;
    std::vector<RepoMapFile> files;
    std::vector<RepoMapSymbol> symbols;
    std::vector<RepoMapDependency> dependencies;
};

std::string to_string(FileKind kind);
std::string to_string(SymbolKind kind);
std::string to_string(DependencyKind kind);

Json to_json(const RepoMapFile& file);
Json to_json(const RepoMapSymbol& symbol);
Json to_json(const RepoMapDependency& dependency);
Json to_json(const RepoMapSummary& summary);
Json to_json(const RepoMapIndex& index);

} // namespace ben_gear::repo_map
