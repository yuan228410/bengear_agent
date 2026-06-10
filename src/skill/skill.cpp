#include "ben_gear/skill/skill.hpp"

#include <fstream>
#include <sstream>

namespace ben_gear::skill {

// ==================== SkillDefinition ====================

std::optional<SkillDefinition> SkillDefinition::from_file(
    const std::filesystem::path& skill_md,
    const container::String& tier) {
    std::ifstream file(skill_md, std::ios::binary);
    if (!file) return std::nullopt;

    std::string content{std::istreambuf_iterator<char>(file),
                         std::istreambuf_iterator<char>()};

    auto fm_start = content.find("---");
    if (fm_start == std::string::npos) return std::nullopt;
    auto fm_end = content.find("---", fm_start + 3);
    if (fm_end == std::string::npos) return std::nullopt;

    std::string frontmatter =
        content.substr(fm_start + 3, fm_end - fm_start - 3);

    SkillDefinition def;
    def.skill_dir = skill_md.parent_path();
    def.tier = tier;
    def.enabled = true;

    std::istringstream fm_stream(frontmatter);
    std::string line;
    while (std::getline(fm_stream, line)) {
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        auto key = line.substr(0, colon);
        auto val = line.substr(colon + 1);
        while (!key.empty() &&
               (key.back() == ' ' || key.back() == '\t'))
            key.pop_back();
        while (!val.empty() &&
               (val.front() == ' ' || val.front() == '\t'))
            val = val.substr(1);
        while (!val.empty() && (val.back() == ' ' || val.back() == '\t' ||
                                val.back() == '\r'))
            val.pop_back();

        if (key == "name")
            def.name = container::String(std::move(val));
        else if (key == "description")
            def.description = container::String(std::move(val));
        else if (key == "version")
            def.version = container::String(std::move(val));
    }

    if (def.name.empty()) {
        def.name = container::String(
            skill_md.parent_path().filename().string());
    }

    return def;
}

container::String SkillDefinition::get_content() const {
    auto skill_md = skill_dir / "SKILL.md";
    std::ifstream file(skill_md, std::ios::binary);
    if (!file) return container::String();

    std::string content{std::istreambuf_iterator<char>(file),
                         std::istreambuf_iterator<char>()};

    // 将相对路径重写为绝对路径
    std::string dir_str = skill_dir.string();
    std::string result;
    std::string::size_type pos = 0;
    while (pos < content.size()) {
        auto found = content.find("scripts/", pos);
        if (found == std::string::npos)
            found = content.find("references/", pos);
        if (found == std::string::npos) {
            result += content.substr(pos);
            break;
        }
        if (found > 0 && content[found - 1] != ' ' &&
            content[found - 1] != '\n' && content[found - 1] != '\t' &&
            content[found - 1] != '(' && content[found - 1] != '/') {
            result += content.substr(pos, found - pos + 8);
            pos = found + 8;
            continue;
        }
        result += content.substr(pos, found - pos);
        result += dir_str + "/";
        pos = found;
        auto end =
            content.find_first_of(" \t\n\r)\",'", found);
        if (end == std::string::npos) end = content.size();
        result += content.substr(found, end - found);
        pos = end;
    }

    return container::String(std::move(result));
}

container::String SkillDefinition::get_metadata_line() const {
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
    return container::String(std::move(line));
}

// ==================== SkillLoader ====================

void SkillLoader::discover() {
    std::map<std::string, SkillDefinition> discovered;
    scan_directory_into("global", global_dir_, discovered);
    scan_directory_into("user", user_dir_, discovered);
    scan_directory_into("project", project_dir_, discovered);

    std::unique_lock lock(mutex_);
    skills_ = std::move(discovered);
    log::info_fmt("skill discovery done: {} skills loaded", skills_.size());
}

bool SkillLoader::has(const std::string& name) const {
    std::shared_lock lock(mutex_);
    return skills_.find(name) != skills_.end();
}

bool SkillLoader::is_enabled(const std::string& name) const {
    std::shared_lock lock(mutex_);
    auto it = skills_.find(name);
    return it != skills_.end() && it->second.enabled;
}

container::String SkillLoader::get_skills_metadata() const {
    std::shared_lock lock(mutex_);
    if (skills_.empty()) return container::String();

    std::string result = "## Available Skills\n";
    for (const auto& [name, skill] : skills_) {
        if (!skill.enabled) continue;
        auto line = skill.get_metadata_line();
        result += line;
        result += '\n';
    }
    return container::String(std::move(result));
}

container::String SkillLoader::get_skill_content(
    const std::string& name) const {
    std::shared_lock lock(mutex_);
    auto it = skills_.find(name);
    if (it == skills_.end()) {
        return container::String("Skill not found: " + name);
    }
    if (!it->second.enabled) {
        return container::String("Skill is disabled: " + name);
    }

    std::string header = "# Skill: ";
    header += name;
    header += "\n\n**Skill Root Directory:** ";
    header += it->second.skill_dir.string();
    header += "\n\n---\n\n";

    auto content = it->second.get_content();
    header.append(content.data(), content.size());
    return container::String(std::move(header));
}

container::Vector<container::String> SkillLoader::enabled_skill_names()
    const {
    std::shared_lock lock(mutex_);
    container::Vector<container::String> names;
    for (const auto& [name, skill] : skills_) {
        if (skill.enabled) {
            names.push_back(container::String(std::move(name)));
        }
    }
    return names;
}

std::filesystem::path SkillLoader::target_dir(
    const std::string& scope) const {
    if (scope == "global") return global_dir_;
    if (scope == "user") return user_dir_;
    return project_dir_;
}

bool SkillLoader::remove_skill(const std::string& name) {
    std::unique_lock lock(mutex_);
    auto it = skills_.find(name);
    if (it == skills_.end()) {
        log::warn_fmt("skill not found for removal: {}", name);
        return false;
    }
    skills_.erase(it);
    log::info_fmt("removed skill '{}' from memory", name);
    return true;
}

bool SkillLoader::enable_skill(const std::string& name) {
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

bool SkillLoader::disable_skill(const std::string& name) {
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

bool SkillLoader::has_skill_in_scope(
    const std::string& name, const std::string& scope) const {
    auto dir = target_dir(scope) / name;
    return std::filesystem::exists(dir / "SKILL.md");
}

void SkillLoader::scan_directory_into(
    const container::String& tier,
    const std::filesystem::path& dir,
    std::map<std::string, SkillDefinition>& out) {
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || ec) return;

    for (const auto& entry :
         std::filesystem::directory_iterator(dir, ec)) {
        if (ec || !entry.is_directory()) continue;

        auto skill_md = entry.path() / "SKILL.md";
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


SkillLoader::SkillLoader(std::filesystem::path global_dir,
                         std::filesystem::path user_dir,
                         std::filesystem::path workspace_dir)
    : global_dir_(std::move(global_dir)),
      user_dir_(std::move(user_dir)),
      project_dir_(std::move(workspace_dir)) {}

}  // namespace ben_gear::skill
