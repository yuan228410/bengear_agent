#pragma once

#include "ben_gear/base/utils/json.hpp"

#include <string>
#include <vector>

namespace ben_gear::git {

struct GitStatusEntry {
    std::string path;
    std::string xy;
    bool staged = false;
    bool unstaged = false;
    bool untracked = false;
};

struct GitStatus {
    bool success = false;
    std::string error_type;
    std::string message;
    std::string repo_root;
    std::string branch;
    bool clean = true;
    std::vector<GitStatusEntry> entries;
};

Json to_json(const GitStatusEntry& entry);
Json to_json(const GitStatus& status);

} // namespace ben_gear::git
