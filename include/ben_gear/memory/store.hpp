#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/map.hpp"
#include "ben_gear/base/platform/file_lock.hpp"
#include "ben_gear/memory/types.hpp"
#include "ben_gear/memory/section_merge.hpp"
#include "ben_gear/base/tier_paths.hpp"

#include <filesystem>
#include <shared_mutex>
#include <vector>

namespace ben_gear::memory {

namespace container = base::container;

/// 三层级记忆存储（跨进程文件锁 + 原子写入保护 + 读缓存）
class MemoryStore {
public:
    explicit MemoryStore(const base::TierPaths& tier_paths);

    /// 读取长期记忆（三层级合并，带缓存）
    container::String read_memory() const;

    /// 读取身份定义（三层级合并，带缓存）
    container::String read_soul() const;

    /// 读取行为规范（三层级合并，带缓存）
    container::String read_rules() const;

    /// 写入长期记忆到指定层级
    void write_memory(const container::String& content, base::Tier tier);

    /// 写入身份定义到指定层级
    void write_soul(const container::String& content, base::Tier tier);

    /// 写入行为规范到指定层级
    void write_rules(const container::String& content, base::Tier tier);

    /// 构建完整合并记忆
    MergedMemory build_merged_memory() const;

    /// 获取层级路径
    const base::TierPaths& tier_paths() const { return tier_paths_; }

    /// 是否有脏数据
    bool is_dirty() const;

    /// 清除脏标记
    void clear_dirty() const;

    /// 强制失效所有缓存
    void invalidate_cache();

private:
    void ensure_directories();
    container::String read_merged(const char* filename) const;
    void write_at(const char* filename,
                  const container::String& content,
                  base::Tier tier);
    static container::String read_file_content(
        const std::filesystem::path& path);

    base::TierPaths tier_paths_;
    mutable container::Map<container::String, container::String> merged_cache_;
    mutable std::shared_mutex cache_mutex_;
    mutable bool dirty_ = false;
};

}  // namespace ben_gear::memory
