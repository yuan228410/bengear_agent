#pragma once

#include <unordered_map>
#include "base/platform/file_lock.hpp"
#include "memory/types.hpp"
#include "memory/section_merge.hpp"
#include "base/tier_paths.hpp"

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
    std::string read_memory() const;

    /// 读取身份定义（三层级合并，带缓存）
    std::string read_soul() const;

    /// 读取行为规范（三层级合并，带缓存）
    std::string read_rules() const;

    /// 写入长期记忆到指定层级
    void write_memory(const std::string& content, base::Tier tier);

    /// 写入身份定义到指定层级
    void write_soul(const std::string& content, base::Tier tier);

    /// 写入行为规范到指定层级
    void write_rules(const std::string& content, base::Tier tier);

    /// 读取用户信息（三层级按优先级取第一个非空，无合并）
    std::string read_user() const;

    /// 写入用户信息到指定层级
    void write_user(const std::string& content, base::Tier tier);

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
    std::string read_merged(const char* filename) const;
    void write_at(const char* filename,
                  const std::string& content,
                  base::Tier tier);
    static std::string read_file_content(
        const std::filesystem::path& path);

    base::TierPaths tier_paths_;
    mutable std::unordered_map<std::string, std::string> merged_cache_;
    mutable std::shared_mutex cache_mutex_;
    mutable bool dirty_ = false;
};

}  // namespace ben_gear::memory
