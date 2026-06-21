#pragma once

#include <string>

namespace ben_gear::workspace_index {

struct WorkspaceIndexOptions {
    int max_files = 2000;
    int max_symbols = 5000;
    int max_dependencies = 5000;
    int max_file_bytes = 1024 * 1024;
    bool include_external = false;
    bool include_hidden = false;
    bool refresh = false;
};

struct WorkspaceIndexMetrics {
    int index_build_count = 0;
    int cache_hit_count = 0;
    int cache_miss_count = 0;
    int invalidated_count = 0;
};

std::string cache_key(const WorkspaceIndexOptions& options, const std::string& project_root, const std::string& signature = {});

} // namespace ben_gear::workspace_index
