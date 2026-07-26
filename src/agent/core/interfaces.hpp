#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "agent/core/core_types.hpp"

namespace ben_gear::agent::core {

class IFileService {
public:
    virtual ~IFileService() = default;
    virtual bool exists(const std::filesystem::path& path) const = 0;
    virtual std::string read(const std::filesystem::path& path) const = 0;
    virtual bool write(const std::filesystem::path& path, const std::string& content) = 0;
    virtual bool remove(const std::filesystem::path& path) = 0;
    virtual bool mkdir(const std::filesystem::path& path) = 0;
    virtual std::vector<std::string> ls(const std::filesystem::path& path) const = 0;
    virtual bool copy(const std::filesystem::path& from, const std::filesystem::path& to) = 0;
    virtual bool rename(const std::filesystem::path& from, const std::filesystem::path& to) = 0;
};

class ICommandExecutor {
public:
    virtual ~ICommandExecutor() = default;
    virtual CommandResult run(const std::string& cmd,
        const std::vector<std::string>& args = {},
        const std::string& cwd = "") = 0;
};

} // namespace ben_gear::agent::core

// ---- 默认实现工厂 ----

namespace ben_gear::agent::core {

std::shared_ptr<IFileService>       make_default_file_service();
std::shared_ptr<ICommandExecutor>   make_default_command_executor();

} // namespace ben_gear::agent::core
