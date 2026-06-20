#pragma once

#include "ben_gear/base/utils/json.hpp"
#include "ben_gear/repo_map/types.hpp"

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

Json location_json(const repo_map::RepoMapSymbol& symbol,
                   std::string preview = {},
                   int end_column = 0,
                   int score = 0);

} // namespace ben_gear::code_intel
