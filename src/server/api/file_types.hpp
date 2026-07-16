#pragma once

#include "server/api/common.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace ben_gear::server {

struct FileEntry {
    std::string name;
    std::string type;   // "file" | "dir"
    int64_t size = 0;
    std::string modified;
};

class FileService {
public:
    virtual ~FileService() = default;

    virtual std::vector<FileEntry> list_files(const std::string& path, const std::string& username) = 0;
    virtual std::string home_directory(const std::string& username) = 0;
};

} // namespace ben_gear::server
