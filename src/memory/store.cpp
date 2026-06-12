#include "ben_gear/memory/store.hpp"

#include <fstream>
#include "ben_gear/base/log/logger.hpp"

namespace ben_gear::memory {

MemoryStore::MemoryStore(const base::TierPaths& tier_paths)
    : tier_paths_(tier_paths) {
    ensure_directories();
}

container::String MemoryStore::read_memory() const {
    return read_merged("MEMORY.md");
}

container::String MemoryStore::read_soul() const {
    return read_merged("SOUL.md");
}

container::String MemoryStore::read_rules() const {
    return read_merged("RULES.md");
}

void MemoryStore::write_memory(const container::String& content,
                                base::Tier tier) {
    write_at("MEMORY.md", content, tier);
}

void MemoryStore::write_soul(const container::String& content,
                              base::Tier tier) {
    write_at("SOUL.md", content, tier);
}

void MemoryStore::write_rules(const container::String& content,
                               base::Tier tier) {
    write_at("RULES.md", content, tier);
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
         {base::Tier::global, base::Tier::user, base::Tier::workspace}) {
        auto dir = tier_paths_.dir(tier) / "memory";
        std::filesystem::create_directories(dir);
    }
}

container::String MemoryStore::read_merged(const char* filename) const {
    {
        std::shared_lock lock(cache_mutex_);
        auto it = merged_cache_.find(container::String(filename));
        if (it != merged_cache_.end()) {
            return it->second;
        }
    }

    container::Vector<container::String> texts;
    for (auto tier :
         {base::Tier::global, base::Tier::user, base::Tier::workspace}) {
        auto path = tier_paths_.dir(tier) / "memory" / filename;
        texts.push_back(read_file_content(path));
    }
    auto result = merge_sections(texts);

    {
        std::unique_lock lock(cache_mutex_);
        merged_cache_[container::String(filename)] = result;
    }

    return result;
}

void MemoryStore::write_at(const char* filename,
                            const container::String& content,
                            base::Tier tier) {
    auto dir = tier_paths_.dir(tier) / "memory";
    std::filesystem::create_directories(dir);
    auto path = dir / filename;

    auto lock = base::platform::FileLock::exclusive(path);
    if (!lock) {
        log::error_fmt("memory write: failed to acquire lock: {}",
                       path.string());
        return;
    }

    if (!lock->truncate(0)) {
        log::error_fmt("memory write: truncate failed: {}", path.string());
        return;
    }

    auto written = lock->write(content.data(), content.size());
    if (written < 0 ||
        static_cast<size_t>(written) != content.size()) {
        log::error_fmt("memory write: write failed: {}", path.string());
        return;
    }

    if (!lock->sync()) {
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
        merged_cache_.erase(container::String(filename));
        dirty_ = true;
    }

    log::info_fmt("memory write: file={} tier={} size={}", filename,
                  base::TierPaths::tier_name(tier), content.size());
}

container::String MemoryStore::read_file_content(
    const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return container::String();

    auto size = file.tellg();
    if (size <= 0) return container::String();
    file.seekg(0, std::ios::beg);

    std::vector<char> buf(static_cast<size_t>(size));
    file.read(buf.data(), static_cast<std::streamsize>(size));
    if (!file) return container::String();
    return container::String(buf.data(), static_cast<size_t>(size));
}

}  // namespace ben_gear::memory
