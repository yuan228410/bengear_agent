#pragma once

#include "server/api/common.hpp"

#include <cstdint>
#include <functional>

namespace ben_gear::server {

struct FileEntry {
    std::string name;
    std::string type;   // "file" | "dir"
    int64_t size = 0;
    std::string modified;
};

using ListFilesFn = std::function<std::vector<FileEntry>(const std::string& path, const std::string& username)>;
using HomeDirectoryFn = std::function<std::string(const std::string& username)>;

struct FileService {
    ListFilesFn list_files;
    HomeDirectoryFn home_directory;
};

} // namespace ben_gear::server
