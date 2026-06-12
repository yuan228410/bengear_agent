#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/vector.hpp"
#include "ben_gear/base/utils/json.hpp"
#include "ben_gear/base/tier_paths.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <shared_mutex>
#include <string>

namespace ben_gear::skill {

namespace container = base::container;

/// 技能定义（从 SKILL.md 解析）
struct SkillDefinition {
    container::String name;
    container::String description;
    container::String version;
    container::String tier;  // "builtin" | "global" | "project"
    std::filesystem::path skill_dir;
    bool enabled = true;

    /// 从 SKILL.md 文件解析
    static std::optional<SkillDefinition> from_file(
        const std::filesystem::path& skill_md,
        const container::String& tier);

    /// 获取完整 SKILL.md 内容（Level 2 按需加载）
    container::String get_content() const;

    /// 获取元数据描述行（Level 1 系统提示注入）
    container::String get_metadata_line() const;
};

/// 技能加载器（目录扫描 + 渐进式披露，线程安全）
class SkillLoader {
public:
    SkillLoader(std::filesystem::path global_dir,
                std::filesystem::path user_dir,
                std::filesystem::path workspace_dir);

    /// 扫描目录，解析 SKILL.md，后层覆盖前层
    void discover();

    bool has(const std::string& name) const;
    bool is_enabled(const std::string& name) const;

    /// 获取所有技能定义（线程安全拷贝）
    std::map<std::string, SkillDefinition> skills() const {
        std::shared_lock lock(mutex_);
        return skills_;
    }

    /// 添加技能定义（用于内置技能等）
    void add_skill(const SkillDefinition& def) {
        std::unique_lock lock(mutex_);
        skills_[std::string(def.name)] = def;
    }

    bool has_skill(const std::string& name) const {
        std::shared_lock lock(mutex_);
        return skills_.find(name) != skills_.end();
    }

    /// Level 1: 系统提示注入（技能名称+描述列表）
    container::String get_skills_metadata() const;

    /// Level 2: 按需加载完整内容
    container::String get_skill_content(const std::string& name) const;

    container::Vector<container::String> enabled_skill_names() const;

    const std::filesystem::path& global_dir() const { return global_dir_; }
    const std::filesystem::path& project_dir() const { return project_dir_; }

    /// 根据 scope 返回目标安装目录
    std::filesystem::path target_dir(const std::string& scope) const;

    /// 移除技能（仅从内存 map 中删除，不删除磁盘）
    bool remove_skill(const std::string& name);

    /// 启用技能
    bool enable_skill(const std::string& name);

    /// 禁用技能
    bool disable_skill(const std::string& name);

    /// 检查指定 scope 目录下是否已有同名技能
    bool has_skill_in_scope(const std::string& name,
                            const std::string& scope) const;

private:
    void scan_directory_into(
        const container::String& tier,
        const std::filesystem::path& dir,
        std::map<std::string, SkillDefinition>& out);

    std::map<std::string, SkillDefinition> skills_;
    mutable std::shared_mutex mutex_;
    std::filesystem::path global_dir_;
    std::filesystem::path user_dir_;
    std::filesystem::path project_dir_;
};

/// 从 TierPaths 构建 SkillLoader（3 层级）
inline SkillLoader make_skill_loader(
    const ben_gear::base::TierPaths& tier_paths) {
    return SkillLoader(
        tier_paths.global_dir / "skills",
        tier_paths.user_dir / "skills",
        tier_paths.workspace_dir / "skills");
}

}  // namespace ben_gear::skill

namespace ben_gear {
using SkillDefinition = skill::SkillDefinition;
using SkillLoader = skill::SkillLoader;
}  // namespace ben_gear
