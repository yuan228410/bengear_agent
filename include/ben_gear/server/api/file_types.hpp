#pragma once

#include "ben_gear/server/api/common.hpp"

#include <cstdint>
#include <functional>

namespace ben_gear::server {

struct FileEntry {
    container::String name;
    container::String type;   // "file" | "dir"
    int64_t size = 0;
    container::String modified;
};

using ListFilesFn = std::function<container::Vector<FileEntry>(const container::String& path, const container::String& username)>;
using HomeDirectoryFn = std::function<container::String(const container::String& username)>;

struct FileService {
    ListFilesFn list_files;
    HomeDirectoryFn home_directory;
};

} // namespace ben_gear::server
