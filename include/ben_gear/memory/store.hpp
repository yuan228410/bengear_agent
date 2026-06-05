#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/log/logger.hpp"
#include "ben_gear/base/platform/file_lock.hpp"
#include "ben_gear/memory/types.hpp"
#include "ben_gear/memory/section_merge.hpp"
#include "ben_gear/workspace/types.hpp"

#include <filesystem>
#include <fstream>
#include <string>

namespace ben_gear::memory {

namespace container = base::container;

/// 三层级记忆存储（跨进程文件锁 + 原子写入保护）
/// 每个文件存储三类内容：MEMORY.md / SOUL.md / RULES.md
/// 读取时三层级 section merge，写入时指定目标层级
class MemoryStore {
public:
    explicit MemoryStore(const workspace::TierPaths& tier_paths)
        : tier_paths_(tier_paths) {
        ensure_directories();
    }

    /// 读取长期记忆（三层级合并）
    container::String read_memory() const {
        return read_merged("MEMORY.md");
    }

    /// 读取身份定义（三层级合并）
    container::String read_soul() const {
        return read_merged("SOUL.md");
    }

    /// 读取行为规范（三层级合并）
    container::String read_rules() const {
        return read_merged("RULES.md");
    }

    /// 写入长期记忆到指定层级
    void write_memory(const container::String& content, workspace::Tier tier) {
        write_at("MEMORY.md", content, tier);
    }

    /// 写入身份定义到指定层级
    void write_soul(const container::String& content, workspace::Tier tier) {
        write_at("SOUL.md", content, tier);
    }

    /// 写入行为规范到指定层级
    void write_rules(const container::String& content, workspace::Tier tier) {
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
    const workspace::TierPaths& tier_paths() const { return tier_paths_; }

private:
    void ensure_directories() {
        for (auto tier : {workspace::Tier::global, workspace::Tier::user, workspace::Tier::workspace}) {
            auto dir = tier_paths_.dir(tier) / "memory_data";
            std::filesystem::create_directories(dir);
        }
    }

    container::String read_merged(const char* filename) const {
        container::Vector<container::String> texts;
        for (auto tier : {workspace::Tier::global, workspace::Tier::user, workspace::Tier::workspace}) {
            auto path = tier_paths_.dir(tier) / "memory_data" / filename;
            texts.push_back(read_file_content(path));
        }
        return merge_sections(texts);
    }

    /// 跨进程安全写入：文件锁 → 写 .tmp → fsync → rename → 解锁
    void write_at(const char* filename,
                  const container::String& content,
                  workspace::Tier tier) {
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

        log::info_fmt("memory write: file={} tier={} size={}",
                      filename, workspace::TierPaths::tier_name(tier), content.size());
        // FileLock RAII 析构时自动释放锁
    }

    static container::String read_file_content(const std::filesystem::path& path) {
        if (!std::filesystem::exists(path)) return container::String();
        std::ifstream file(path, std::ios::binary);
        if (!file) return container::String();
        std::string content{
            std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>()};
        return container::String(content.c_str());
    }

    workspace::TierPaths tier_paths_;
};

}  // namespace ben_gear::memory
