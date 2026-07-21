#include "memory/store.hpp"
#include <shared_mutex>
#include <filesystem>
#include <mutex>

#include <fstream>
#include "platform/file_lock.hpp"
#include "memory/section_merge.hpp"
#include "log/logger.hpp"

namespace ben_gear::memory {

MemoryStore::MemoryStore(const base::TierPaths& tier_paths)
    : tier_paths_(tier_paths) {
    ensure_directories();
}

std::string MemoryStore::read_memory() const {
    return read_merged("MEMORY.md");
}

std::string MemoryStore::read_soul() const {
    return read_merged("SOUL.md");
}

std::string MemoryStore::read_rules() const {
    return read_merged("RULES.md");
}

void MemoryStore::write_memory(const std::string& content,
                                base::Tier tier) {
    write_at("MEMORY.md", content, tier);
}

void MemoryStore::write_soul(const std::string& content,
                              base::Tier tier) {
    write_at("SOUL.md", content, tier);
}

void MemoryStore::write_rules(const std::string& content,
                               base::Tier tier) {
    write_at("RULES.md", content, tier);
}

std::string MemoryStore::read_user() const {
    // USER.md 按优先级取第一个存在的（不合并）
    for (auto tier :
         {base::Tier::workspace, base::Tier::global}) {
        auto path = tier_paths_.dir(tier) / "memory" / "USER.md";
        auto content = read_file_content(path);
        if (!content.empty()) return content;
    }
    return {};
}

void MemoryStore::write_user(const std::string& content,
                              base::Tier tier) {
    write_at("USER.md", content, tier);
}

MergedMemory MemoryStore::build_merged_memory() const {
    return {read_memory(), read_soul(), read_rules()};
}

bool MemoryStore::is_dirty() const {
    std::shared_lock lock(cache_mutex_);
    return dirty_;
}

void MemoryStore::clear_dirty() const {
    std::unique_lock lock(cache_mutex_);
    dirty_ = false;
}

void MemoryStore::invalidate_cache() {
    std::unique_lock lock(cache_mutex_);
    merged_cache_.clear();
    dirty_ = false;
}

void MemoryStore::ensure_directories() {
    for (auto tier :
         {base::Tier::global, base::Tier::workspace}) {
        auto dir = tier_paths_.dir(tier) / "memory";
        std::filesystem::create_directories(dir);
    }
}

std::string MemoryStore::read_merged(const char* filename) const {
    {
        std::shared_lock lock(cache_mutex_);
        auto it = merged_cache_.find(std::string(filename));
        if (it != merged_cache_.end()) {
            return it->second;
        }
    }

    std::vector<std::string> texts;
    for (auto tier :
         {base::Tier::global, base::Tier::workspace}) {
        auto path = tier_paths_.dir(tier) / "memory" / filename;
        texts.push_back(read_file_content(path));
    }
    auto result = merge_sections(texts);

    {
        std::unique_lock lock(cache_mutex_);
        merged_cache_[std::string(filename)] = result;
    }

    return result;
}

void MemoryStore::write_at(const char* filename,
                            const std::string& content,
                            base::Tier tier) {
    auto dir = tier_paths_.dir(tier) / "memory";
    std::filesystem::create_directories(dir);
    auto path = dir / filename;

    auto file_lock = base::platform::FileLock::exclusive(path);
    if (!file_lock) {
        log::error_fmt("memory write: failed to acquire lock: {}",
                       path.string());
        return;
    }

    if (!file_lock->truncate(0)) {
        log::error_fmt("memory write: failed to truncate: {}", path.string());
        return;
    }

    auto written = file_lock->write(content.data(), content.size());
    if (written != static_cast<ssize_t>(content.size())) {
        log::error_fmt("memory write: partial write ({}/{}) to {}",
                       written, content.size(), path.string());
        return;
    }

    if (!file_lock->sync()) {
        log::warn_fmt("memory write: fsync failed: {}", path.string());
    }

    try {
        std::filesystem::permissions(
            path,
            std::filesystem::perms::owner_read |
                std::filesystem::perms::owner_write,
            std::filesystem::perm_options::replace);
    } catch (const std::exception& e) {
        log::warn_fmt("memory write: chmod failed: {}", e.what());
    }

    {
        std::unique_lock lock(cache_mutex_);
        merged_cache_.erase(std::string(filename));
        dirty_ = true;
    }

    log::info_fmt("memory write: file={} tier={} size={}", filename,
                  base::TierPaths::tier_name(tier), content.size());
}

std::string MemoryStore::read_file_content(
    const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return std::string();

    auto size = file.tellg();
    if (size <= 0) return std::string();
    file.seekg(0, std::ios::beg);

    std::vector<char> buf(static_cast<size_t>(size));
    file.read(buf.data(), static_cast<std::streamsize>(size));
    if (!file) return std::string();
    return std::string(buf.data(), static_cast<size_t>(size));
}

}  // namespace ben_gear::memory
