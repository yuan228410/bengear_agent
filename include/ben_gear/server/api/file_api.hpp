#pragma once
#include "ben_gear/server/core/router.hpp"
#include "ben_gear/server/api/deps.hpp"

#include <functional>

namespace ben_gear::server {

// ---- 文件服务 ----
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

void register_file_routes(Router& router, FileService& svc);

} // namespace ben_gear::server
