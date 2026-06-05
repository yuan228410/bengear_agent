#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/vector.hpp"
#include "ben_gear/base/utils/json.hpp"
#include "ben_gear/base/platform/os.hpp"
#include "ben_gear/base/platform/platform.hpp"
#include "ben_gear/base/log/logger.hpp"
#include "ben_gear/workspace/types.hpp"

#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <shared_mutex>
#include <sstream>
#include <string>

namespace ben_gear::skill {

namespace container = base::container;

/// 技能定义（从 SKILL.md 解析）
struct SkillDefinition {
    container::String name;
    container::String description;
    container::String version;
    container::String tier;           // "builtin" | "global" | "project"
    std::filesystem::path skill_dir;  // 技能目录路径
    bool enabled = true;

    /// 从 SKILL.md 文件解析（YAML frontmatter + Markdown 正文）
    static std::optional<SkillDefinition> from_file(
        const std::filesystem::path& skill_md,
        const container::String& tier) {
        std::ifstream file(skill_md, std::ios::binary);
        if (!file) return std::nullopt;

        std::string content{
            std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>()};

        // 解析 frontmatter (--- ... ---)，仅支持 key: value 单行格式
        auto fm_start = content.find("---");
        if (fm_start == std::string::npos) return std::nullopt;
        auto fm_end = content.find("---", fm_start + 3);
        if (fm_end == std::string::npos) return std::nullopt;

        std::string frontmatter = content.substr(fm_start + 3, fm_end - fm_start - 3);

        SkillDefinition def;
        def.skill_dir = skill_md.parent_path();
        def.tier = tier;
        def.enabled = true;

        // 简单 key: value 解析（每行一个字段，无嵌套）
        std::istringstream fm_stream(frontmatter);
        std::string line;
        while (std::getline(fm_stream, line)) {
            auto colon = line.find(':');
            if (colon == std::string::npos) continue;
            auto key = line.substr(0, colon);
            auto val = line.substr(colon + 1);
            // trim
            while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
            while (!val.empty() && (val.front() == ' ' || val.front() == '\t')) val = val.substr(1);
            while (!val.empty() && (val.back() == ' ' || val.back() == '\t' || val.back() == '\r')) val.pop_back();

            if (key == "name") def.name = container::String(val.c_str());
            else if (key == "description") def.description = container::String(val.c_str());
            else if (key == "version") def.version = container::String(val.c_str());
        }

        if (def.name.empty()) {
            // 从目录名推断
            def.name = container::String(skill_md.parent_path().filename().string().c_str());
        }

        return def;
    }

    /// 获取完整 SKILL.md 内容（Level 2 按需加载）
    container::String get_content() const {
        auto skill_md = skill_dir / "SKILL.md";
        std::ifstream file(skill_md, std::ios::binary);
        if (!file) return container::String();

        std::string content{
            std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>()};

        // 将相对路径重写为绝对路径
        std::string dir_str = skill_dir.string();
        std::string result;
        std::string::size_type pos = 0;
        while (pos < content.size()) {
            // 查找 scripts/ 或 references/ 的相对路径引用
            auto found = content.find("scripts/", pos);
            if (found == std::string::npos) found = content.find("references/", pos);
            if (found == std::string::npos) {
                result += content.substr(pos);
                break;
            }
            // 检查前面是否是路径分隔符或空白
            if (found > 0 && content[found - 1] != ' ' && content[found - 1] != '\n' &&
                content[found - 1] != '\t' && content[found - 1] != '(' && content[found - 1] != '/') {
                result += content.substr(pos, found - pos + 8);
                pos = found + 8;
                continue;
            }
            result += content.substr(pos, found - pos);
            result += dir_str + "/";
            pos = found;
            // 跳过 scripts/ 或 references/，保留后续路径
            auto end = content.find_first_of(" \t\n\r)\",'", found);
            if (end == std::string::npos) end = content.size();
            result += content.substr(found, end - found);
            pos = end;
        }

        return container::String(result.c_str());
    }

    /// 获取元数据描述行（Level 1 系统提示注入）
    container::String get_metadata_line() const {
        std::string line = "- ";
        line += name;
        line += ": ";
        line += description;
        if (!version.empty()) {
            line += " (v";
            line += version;
            line += ")";
        }
        if (!tier.empty() && std::string_view(tier) != "builtin") {
            line += " [";
            line += tier;
            line += "]";
        }
        return container::String(line.c_str());
    }
};

/// 技能加载器（目录扫描 + 渐进式披露，线程安全）
class SkillLoader {
public:
    /// 旧接口：2 层级（向后兼容）
    SkillLoader(std::filesystem::path global_dir,
                std::filesystem::path project_dir)
        : global_dir_(std::move(global_dir)),
          user_dir_(),
          project_dir_(std::move(project_dir)) {}

    /// 新接口：3 层级
    SkillLoader(std::filesystem::path global_dir,
                std::filesystem::path user_dir,
                std::filesystem::path workspace_dir)
        : global_dir_(std::move(global_dir)),
          user_dir_(std::move(user_dir)),
          project_dir_(std::move(workspace_dir)) {}

    /// 扫描目录，解析 SKILL.md，后层覆盖前层
    void discover() {
        std::map<std::string, SkillDefinition> discovered;
        scan_directory_into("global", global_dir_, discovered);
        if (!user_dir_.empty()) {
            scan_directory_into("user", user_dir_, discovered);
        }
        scan_directory_into("project", project_dir_, discovered);
        std::unique_lock lock(mutex_);
        skills_ = std::move(discovered);
    }

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

    bool is_enabled(const std::string& name) const {
        std::shared_lock lock(mutex_);
        auto it = skills_.find(name);
        return it != skills_.end() && it->second.enabled;
    }

    /// Level 1: 系统提示注入（技能名称+描述列表）
    container::String get_skills_metadata() const {
        std::shared_lock lock(mutex_);
        if (skills_.empty()) return container::String();

        std::string result = "## Available Skills\n";
        for (const auto& [name, skill] : skills_) {
            if (!skill.enabled) continue;
            auto line = skill.get_metadata_line();
            result += line;
            result += '\n';
        }
        return container::String(result.c_str());
    }

    /// Level 2: 按需加载完整内容
    container::String get_skill_content(const std::string& name) const {
        std::shared_lock lock(mutex_);
        auto it = skills_.find(name);
        if (it == skills_.end()) {
            return container::String(("Skill not found: " + name).c_str());
        }
        if (!it->second.enabled) {
            return container::String(("Skill is disabled: " + name).c_str());
        }

        std::string header = "# Skill: ";
        header += name;
        header += "\n\n**Skill Root Directory:** ";
        header += it->second.skill_dir.string();
        header += "\n\n---\n\n";

        auto content = it->second.get_content();
        header.append(content.data(), content.size());
        return container::String(header.c_str());
    }

    container::Vector<container::String> enabled_skill_names() const {
        std::shared_lock lock(mutex_);
        container::Vector<container::String> names;
        for (const auto& [name, skill] : skills_) {
            if (skill.enabled) {
                names.push_back(container::String(name.c_str()));
            }
        }
        return names;
    }

    const std::filesystem::path& global_dir() const { return global_dir_; }
    const std::filesystem::path& project_dir() const { return project_dir_; }

    /// 根据 scope 返回目标安装目录
    std::filesystem::path target_dir(const std::string& scope) const {
        if (scope == "global") return global_dir_;
        if (scope == "user") return user_dir_;
        return project_dir_;
    }

    /// 移除技能（仅从内存 map 中删除，不删除磁盘）
    bool remove_skill(const std::string& name) {
        std::unique_lock lock(mutex_);
        auto it = skills_.find(name);
        if (it == skills_.end()) {
            log::warn_fmt("skill not found for removal: {}", name);
            return false;
        }
        auto dir = it->second.skill_dir;
        skills_.erase(it);
        log::info_fmt("removed skill '{}' from memory", name);
        return true;
    }

    /// 启用技能
    bool enable_skill(const std::string& name) {
        std::filesystem::path sentinel;
        {
            std::unique_lock lock(mutex_);
            auto it = skills_.find(name);
            if (it == skills_.end()) {
                log::warn_fmt("skill not found for enable: {}", name);
                return false;
            }
            it->second.enabled = true;
            sentinel = it->second.skill_dir / ".disabled";
        }
        std::error_code ec;
        std::filesystem::remove(sentinel, ec);
        log::info_fmt("enabled skill: {}", name);
        return true;
    }

    /// 禁用技能
    bool disable_skill(const std::string& name) {
        std::filesystem::path sentinel;
        {
            std::unique_lock lock(mutex_);
            auto it = skills_.find(name);
            if (it == skills_.end()) {
                log::warn_fmt("skill not found for disable: {}", name);
                return false;
            }
            it->second.enabled = false;
            sentinel = it->second.skill_dir / ".disabled";
        }
        std::ofstream of(sentinel);
        if (of) of << "disabled";
        log::info_fmt("disabled skill: {}", name);
        return true;
    }

    /// 检查指定 scope 目录下是否已有同名技能
    bool has_skill_in_scope(const std::string& name, const std::string& scope) const {
        auto dir = target_dir(scope) / name;
        return std::filesystem::exists(dir / "SKILL.md");
    }

private:
    void scan_directory_into(const container::String& tier,
                             const std::filesystem::path& dir,
                             std::map<std::string, SkillDefinition>& out) {
        if (!std::filesystem::exists(dir)) return;

        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (!entry.is_directory()) continue;

            auto skill_md = entry.path() / "SKILL.md";
            if (!std::filesystem::exists(skill_md)) continue;

            auto def = SkillDefinition::from_file(skill_md, tier);
            if (def) {
                auto sentinel = entry.path() / ".disabled";
                if (std::filesystem::exists(sentinel)) {
                    def->enabled = false;
                }
                auto name = std::string(def->name);
                out[name] = std::move(*def);
                log::info_fmt("discovered skill: {} [{}]", name, tier);
            }
        }
    }

    std::map<std::string, SkillDefinition> skills_;
    mutable std::shared_mutex mutex_;
    std::filesystem::path global_dir_;
    std::filesystem::path user_dir_;      // 新增：用户级技能目录
    std::filesystem::path project_dir_;
};

/// 从 workspace 构建 SkillLoader（单层级）
inline SkillLoader make_skill_loader(const std::filesystem::path& workspace) {
    auto global_dir = support::data_directory() / "skills";
    auto project_dir = workspace / ".bengear" / "skills";
    return SkillLoader(global_dir, project_dir);
}

/// 从 TierPaths 构建 SkillLoader（新接口，3 层级）
inline SkillLoader make_skill_loader(const ben_gear::workspace::TierPaths& tier_paths) {
    return SkillLoader(
        tier_paths.global_dir / "skills",
        tier_paths.user_dir / "skills",
        tier_paths.workspace_dir / "skills"
    );
}

}  // namespace ben_gear::skill

namespace ben_gear {
using SkillDefinition = skill::SkillDefinition;
using SkillLoader = skill::SkillLoader;
}  // namespace ben_gear
