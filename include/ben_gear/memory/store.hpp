#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/map.hpp"
#include "ben_gear/base/log/logger.hpp"
#include "ben_gear/base/platform/file_lock.hpp"
#include "ben_gear/memory/types.hpp"
#include "ben_gear/memory/section_merge.hpp"
#include "ben_gear/base/tier_paths.hpp"

#include <filesystem>
#include <fstream>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

namespace ben_gear::memory {

namespace container = base::container;

/// 三层级记忆存储（跨进程文件锁 + 原子写入保护 + 读缓存）
/// 每个文件存储三类内容：MEMORY.md / SOUL.md / RULES.md
/// 读取时三层级 section merge，写入时指定目标层级并自动失效缓存
class MemoryStore {
public:
    explicit MemoryStore(const base::TierPaths& tier_paths)
        : tier_paths_(tier_paths) {
        ensure_directories();
    }

    /// 读取长期记忆（三层级合并，带缓存）
    container::String read_memory() const {
        return read_merged("MEMORY.md");
    }

    /// 读取身份定义（三层级合并，带缓存）
    container::String read_soul() const {
        return read_merged("SOUL.md");
    }

    /// 读取行为规范（三层级合并，带缓存）
    container::String read_rules() const {
        return read_merged("RULES.md");
    }

    /// 写入长期记忆到指定层级
    void write_memory(const container::String& content, base::Tier tier) {
        write_at("MEMORY.md", content, tier);
    }

    /// 写入身份定义到指定层级
    void write_soul(const container::String& content, base::Tier tier) {
        write_at("SOUL.md", content, tier);
    }

    /// 写入行为规范到指定层级
    void write_rules(const container::String& content, base::Tier tier) {
        write_at("RULES.md", content, tier);
    }

    /// 构建完整合并记忆
    MergedMemory build_merged_memory() const {
        return {
            read_memory(),
            read_soul(),
            read_rules()
        };
    }

    /// 获取层级路径
    const base::TierPaths& tier_paths() const { return tier_paths_; }

    /// 是否有脏数据（写入后未重新 build）
    bool is_dirty() const {
        std::shared_lock lock(cache_mutex_);
        return dirty_;
    }

    /// 清除脏标记（build 完成后调用）
    void clear_dirty() const {
        std::unique_lock lock(cache_mutex_);
        dirty_ = false;
    }

    /// 强制失效所有缓存（外部修改文件后调用）
    void invalidate_cache() {
        std::unique_lock lock(cache_mutex_);
        merged_cache_.clear();
        dirty_ = false;
    }

private:
    void ensure_directories() {
        for (auto tier : {base::Tier::global, base::Tier::user, base::Tier::workspace}) {
            auto dir = tier_paths_.dir(tier) / "memory_data";
            std::filesystem::create_directories(dir);
        }
    }

    /// 读取合并结果（带缓存）
    container::String read_merged(const char* filename) const {
        {
            std::shared_lock lock(cache_mutex_);
            auto it = merged_cache_.find(container::String(filename));
            if (it != merged_cache_.end()) {
                return it->second;
            }
        }

        // 缓存未命中：读文件 + 合并
        container::Vector<container::String> texts;
        for (auto tier : {base::Tier::global, base::Tier::user, base::Tier::workspace}) {
            auto path = tier_paths_.dir(tier) / "memory_data" / filename;
            texts.push_back(read_file_content(path));
        }
        auto result = merge_sections(texts);

        // 写入缓存
        {
            std::unique_lock lock(cache_mutex_);
            merged_cache_[container::String(filename)] = result;
        }

        return result;
    }

    /// 跨进程安全写入：文件锁 → truncate → write → fsync → chmod → 解锁
    /// 写入后自动失效对应的合并缓存
    void write_at(const char* filename,
                  const container::String& content,
                  base::Tier tier) {
        auto dir = tier_paths_.dir(tier) / "memory_data";
        std::filesystem::create_directories(dir);
        auto path = dir / filename;

        // 获取文件锁（跨进程互斥）
        auto lock = base::platform::FileLock::exclusive(path);
        if (!lock) {
            log::error_fmt("memory write: failed to acquire lock: {}", path.string());
            return;
        }

        // 截断文件并写入新内容
        if (!lock->truncate(0)) {
            log::error_fmt("memory write: truncate failed: {}", path.string());
            return;
        }

        auto written = lock->write(content.data(), content.size());
        if (written < 0 || static_cast<size_t>(written) != content.size()) {
            log::error_fmt("memory write: write failed: {}", path.string());
            return;
        }

        // fsync 确保数据落盘
        if (!lock->sync()) {
            log::warn_fmt("memory write: fsync failed: {}", path.string());
        }

        // 设置文件权限为 600（仅所有者可读写），保护敏感信息
        try {
            std::filesystem::permissions(path,
                std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                std::filesystem::perm_options::replace);
        } catch (const std::exception& e) {
            log::warn_fmt("memory write: chmod failed: {}", e.what());
        }

        // 失效合并缓存（写入了任意层级，合并结果必须重新计算）
        {
            std::unique_lock lock(cache_mutex_);
            merged_cache_.erase(container::String(filename));
            dirty_ = true;
        }

        log::info_fmt("memory write: file={} tier={} size={}",
                      filename, base::TierPaths::tier_name(tier), content.size());
        // FileLock RAII 析构时自动释放锁
    }

    /// 读取单个文件内容（零拷贝优化：seek/tell + 单次 vector + 直接构造）
    static container::String read_file_content(const std::filesystem::path& path) {
        if (!std::filesystem::exists(path)) return container::String();
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) return container::String();

        auto size = file.tellg();
        if (size <= 0) return container::String();
        file.seekg(0, std::ios::beg);

        // 单次分配：文件 → vector → String，避免 istreambuf_iterator 逐字符读取
        std::vector<char> buf(static_cast<size_t>(size));
        file.read(buf.data(), static_cast<std::streamsize>(size));
        if (!file) return container::String();
        return container::String(buf.data(), static_cast<size_t>(size));
    }

    base::TierPaths tier_paths_;

    /// 合并结果缓存：filename → merged content
    mutable container::Map<container::String, container::String> merged_cache_;
    mutable std::shared_mutex cache_mutex_;
    mutable bool dirty_ = false;
};

}  // namespace ben_gear::memory
