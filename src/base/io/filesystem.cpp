#include "base/io/filesystem.hpp"
#include <fstream>
#include <sstream>

namespace ben_gear::base::io {

FileResult RealFileSystem::read_file(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return FileResult::failure("cannot open file: " + path.string());
    }
    std::ostringstream oss;
    oss << file.rdbuf();
    return FileResult::success(oss.str());
}

std::vector<uint8_t> RealFileSystem::read_binary(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return {};
    }
    auto size = file.tellg();
    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer(static_cast<size_t>(size));
    file.read(reinterpret_cast<char*>(buffer.data()), size);
    return buffer;
}

bool RealFileSystem::exists(const std::filesystem::path& path) {
    return std::filesystem::exists(path);
}

uint64_t RealFileSystem::file_size(const std::filesystem::path& path) {
    return std::filesystem::file_size(path);
}

bool RealFileSystem::is_directory(const std::filesystem::path& path) {
    return std::filesystem::is_directory(path);
}

bool RealFileSystem::is_regular_file(const std::filesystem::path& path) {
    return std::filesystem::is_regular_file(path);
}

std::vector<std::filesystem::path> RealFileSystem::list_directory(
    const std::filesystem::path& dir) {
    std::vector<std::filesystem::path> result;
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        result.push_back(entry.path());
    }
    return result;
}

std::vector<std::filesystem::path> RealFileSystem::list_directory_recursive(
    const std::filesystem::path& dir) {
    std::vector<std::filesystem::path> result;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
        result.push_back(entry.path());
    }
    return result;
}

FileResult RealFileSystem::write_file(const std::filesystem::path& path,
                                      std::string_view content) {
    std::ofstream file(path, std::ios::trunc);
    if (!file.is_open()) {
        return FileResult::failure("cannot open file for writing: " + path.string());
    }
    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    return FileResult::success();
}

FileResult RealFileSystem::append_file(const std::filesystem::path& path,
                                       std::string_view content) {
    std::ofstream file(path, std::ios::app);
    if (!file.is_open()) {
        return FileResult::failure("cannot open file for appending: " + path.string());
    }
    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    return FileResult::success();
}

FileResult RealFileSystem::create_directories(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec) {
        return FileResult::failure("create_directories failed: " + ec.message());
    }
    return FileResult::success();
}

FileResult RealFileSystem::remove(const std::filesystem::path& path) {
    std::error_code ec;
    bool removed = std::filesystem::remove(path, ec);
    if (ec) {
        return FileResult::failure("remove failed: " + ec.message());
    }
    if (!removed) {
        return FileResult::failure("file not found: " + path.string());
    }
    return FileResult::success();
}

FileResult RealFileSystem::remove_all(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    if (ec) {
        return FileResult::failure("remove_all failed: " + ec.message());
    }
    return FileResult::success();
}

FileResult RealFileSystem::rename(const std::filesystem::path& from,
                                  const std::filesystem::path& to) {
    std::error_code ec;
    std::filesystem::rename(from, to, ec);
    if (ec) {
        return FileResult::failure("rename failed: " + ec.message());
    }
    return FileResult::success();
}

FileResult RealFileSystem::copy_file(const std::filesystem::path& from,
                                     const std::filesystem::path& to) {
    std::error_code ec;
    std::filesystem::copy_file(from, to, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        return FileResult::failure("copy_file failed: " + ec.message());
    }
    return FileResult::success();
}

std::filesystem::path RealFileSystem::current_path() {
    return std::filesystem::current_path();
}

std::filesystem::path RealFileSystem::extension(const std::filesystem::path& path) {
    return path.extension();
}

std::filesystem::path RealFileSystem::parent_path(const std::filesystem::path& path) {
    return path.parent_path();
}

std::filesystem::path RealFileSystem::weakly_canonical(const std::filesystem::path& path) {
    return std::filesystem::weakly_canonical(path);
}

std::filesystem::path RealFileSystem::relative(const std::filesystem::path& path,
                                               const std::filesystem::path& base) {
    return std::filesystem::relative(path, base);
}

} // namespace ben_gear::base::io
