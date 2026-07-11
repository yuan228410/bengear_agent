#pragma once

#include "base/utils/json.hpp"
#include "intelligence/repo_map/types.hpp"

#include <string>

namespace ben_gear::code_intel {

struct CodeIntelQuery {
    std::string path;
    int line = 0;
    int column = 0;
    std::string symbol;
    int limit = 50;
};

struct CodeIntelOptions {
    int max_files = 2000;
    int max_symbols = 5000;
    int max_file_bytes = 1024 * 1024;
    int max_references = 100;
    bool include_external = false;
    bool include_hidden = false;
};

struct CodeIntelCapabilitiesResult {
    bool document_symbols = true;
    bool definition = true;
    bool references = true;
    bool workspace_symbols = true;
    bool hover = false;
    bool rename = false;
    bool code_actions = false;
};

struct CodeIntelLocation {
    std::string path;
    int line = 0;
    int column = 0;
    int end_column = 0;
    std::string symbol;
    std::string kind;
    std::string signature;
    std::string container;
    std::string language;
    std::string preview;
    int score = 0;
};

struct CodeIntelDocumentSymbolsResult {
    std::string path;
    std::vector<CodeIntelLocation> symbols;
};

struct CodeIntelWorkspaceSymbolsResult {
    std::string query;
    std::string kind;
    std::string language;
    std::vector<CodeIntelLocation> symbols;
    bool truncated = false;
};

struct CodeIntelDefinitionResult {
    std::string symbol;
    std::vector<CodeIntelLocation> definitions;
    bool truncated = false;
};

struct CodeIntelReferencesResult {
    std::string symbol;
    std::vector<CodeIntelLocation> references;
    int scanned_files = 0;
    bool truncated = false;
};

CodeIntelLocation location_from_symbol(const repo_map::RepoMapSymbol& symbol,
                                       std::string preview = {},
                                       int end_column = 0,
                                       int score = 0);

Json location_json(const CodeIntelLocation& location);
Json location_json(const repo_map::RepoMapSymbol& symbol,
                   std::string preview = {},
                   int end_column = 0,
                   int score = 0);
Json to_json(const CodeIntelCapabilitiesResult& result);
Json to_json(const CodeIntelDocumentSymbolsResult& result);
Json to_json(const CodeIntelWorkspaceSymbolsResult& result);
Json to_json(const CodeIntelDefinitionResult& result);
Json to_json(const CodeIntelReferencesResult& result);

} // namespace ben_gear::code_intel
