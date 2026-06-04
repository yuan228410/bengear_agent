#pragma once

#include "ben_gear/skill/skill.hpp"
#include "ben_gear/skill/zip_extract.hpp"
#include "ben_gear/tools/builtin_tools.hpp"
#include "ben_gear/tool/registry.hpp"
#include "ben_gear/base/log/logger.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace ben_gear::tools {

using namespace ben_gear::llm;
using SkillDefinition = ben_gear::skill::SkillDefinition;
using SkillLoader = ben_gear::skill::SkillLoader;
using skill::download_file;
using skill::extract_zip;

/// 获取内置技能定义列表
inline base::container::Vector<SkillDefinition> builtin_skill_definitions() {
    base::container::Vector<SkillDefinition> defs;

    {
        SkillDefinition d;
        d.name = base::container::String("file_tools");
        d.version = base::container::String("1.0.0");
        d.description = base::container::String("File read/write/delete/list/rename operations");
        d.tier = base::container::String("builtin");
        d.enabled = true;
        defs.push_back(d);
    }
    {
        SkillDefinition d;
        d.name = base::container::String("shell_tools");
        d.version = base::container::String("1.0.0");
        d.description = base::container::String("Shell command execution");
        d.tier = base::container::String("builtin");
        d.enabled = true;
        defs.push_back(d);
    }
    {
        SkillDefinition d;
        d.name = base::container::String("http_tools");
        d.version = base::container::String("1.0.0");
        d.description = base::container::String("HTTP request tools");
        d.tier = base::container::String("builtin");
        d.enabled = true;
        defs.push_back(d);
    }

    return defs;
}

/// 注册 get_skill 工具（Level 2 按需加载）
inline void register_skill_tools(ToolRegistry& registry, SkillLoader* loader) {
    if (!loader) return;

    registry.register_tool(
        base::container::String("get_skill"),
        base::container::String("Load a skill's full content by name. Use this when you need detailed instructions for a skill."),
        {
            {base::container::String("name"), ToolParameterSchema{
                .type = base::container::String("string"),
                .description = base::container::String("Skill name to load")
            }}
        },
        [loader](const Json& args) -> container::String {
            auto name = args.value("name", "");
            auto content = loader->get_skill_content(name);
            log::info_fmt("get_skill: name={} content_len={}", name, content.size());
            return container::String(content.c_str());
        }
    );

    log::info_fmt("registered skill tools: get_skill");
}

/// 生成临时目录路径
inline std::string make_temp_dir() {
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::random_device rd;
    return "/tmp/bengear_skill_" + std::to_string(now) + "_" + std::to_string(rd());
}

/// 注册技能管理工具（install, remove, enable, disable, list）
inline void register_skill_management_tools(ToolRegistry& registry,
                                             SkillLoader* loader,
                                             ToolRegistry* /*tool_registry*/) {
    if (!loader) return;

    // ── install_skill ──────────────────────────────────────
    registry.register_tool(
        base::container::String("install_skill"),
        base::container::String("Install a skill from a remote zip URL, local zip file, or local directory. "
                          "Scope 'project' installs to <workspace>/.bengear/skills/, 'global' to ~/.bengear/skills/."),
        {
            {base::container::String("source"), ToolParameterSchema{
                .type = base::container::String("string"),
                .description = base::container::String("Source: remote zip URL (https://...), local zip path, or local directory path")
            }},
            {base::container::String("scope"), ToolParameterSchema{
                .type = base::container::String("string"),
                .description = base::container::String("Installation scope: 'project' (default) or 'global'")
            }}
        },
        [loader](const Json& args) -> container::String {
            std::string source = args["source"].get<std::string>();
            std::string scope = args.value("scope", "project");

            log::info_fmt("install_skill: source='{}' scope='{}'", source, scope);

            auto target_base = loader->target_dir(scope);
            std::error_code ec;
            std::filesystem::create_directories(target_base, ec);
            if (ec) {
                log::error_fmt("failed to create target dir: {}", ec.message());
                return container::String(Json{{"success", false}, {"error", "Failed to create target directory: " + ec.message()}}.dump().c_str());
            }

            bool is_url = source.rfind("http://", 0) == 0 || source.rfind("https://", 0) == 0;
            bool is_zip = !is_url && source.size() >= 4 && source.substr(source.size() - 4) == ".zip";

            std::string temp_dir;
            std::string zip_path;

            if (is_url) {
                temp_dir = make_temp_dir();
                std::filesystem::create_directories(temp_dir, ec);
                zip_path = temp_dir + "/download.zip";
                log::info_fmt("downloading remote zip: {} -> {}", source, zip_path);
                if (!download_file(source, zip_path)) {
                    std::filesystem::remove_all(temp_dir, ec);
                    return container::String(Json{{"success", false}, {"error", "Download failed: " + source}}.dump().c_str());
                }
                is_zip = true;
            } else if (is_zip) {
                zip_path = source;
                temp_dir = make_temp_dir();
                std::filesystem::create_directories(temp_dir, ec);
            }

            std::string staging_dir;
            if (is_zip) {
                staging_dir = make_temp_dir() + "_extract";
                std::filesystem::create_directories(staging_dir, ec);
                log::info_fmt("extracting zip: {} -> {}", zip_path, staging_dir);
                if (!extract_zip(zip_path, staging_dir)) {
                    std::filesystem::remove_all(temp_dir, ec);
                    std::filesystem::remove_all(staging_dir, ec);
                    return container::String(Json{{"success", false}, {"error", "Zip extraction failed: " + zip_path}}.dump().c_str());
                }
            } else {
                staging_dir = source;
            }

            std::filesystem::path skill_src;
            auto staging_path = std::filesystem::path(staging_dir);

            if (std::filesystem::exists(staging_path / "SKILL.md")) {
                skill_src = staging_path;
            } else {
                bool found = false;
                for (const auto& entry : std::filesystem::directory_iterator(staging_path)) {
                    if (entry.is_directory() && std::filesystem::exists(entry.path() / "SKILL.md")) {
                        skill_src = entry.path();
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    std::filesystem::remove_all(temp_dir, ec);
                    if (is_zip) std::filesystem::remove_all(staging_dir, ec);
                    log::error_fmt("no SKILL.md found in: {}", staging_dir);
                    return container::String(Json{{"success", false}, {"error", "No SKILL.md found in source"}}.dump().c_str());
                }
            }

            auto def = SkillDefinition::from_file(skill_src / "SKILL.md", base::container::String(scope.c_str()));
            if (!def) {
                std::filesystem::remove_all(temp_dir, ec);
                if (is_zip) std::filesystem::remove_all(staging_dir, ec);
                log::error_fmt("failed to parse SKILL.md: {}", (skill_src / "SKILL.md").string());
                return container::String(Json{{"success", false}, {"error", "Failed to parse SKILL.md"}}.dump().c_str());
            }

            std::string skill_name = std::string(def->name);

            std::string other_scope = (scope == "global") ? "project" : "global";
            if (loader->has_skill_in_scope(skill_name, other_scope)) {
                std::filesystem::remove_all(temp_dir, ec);
                if (is_zip) std::filesystem::remove_all(staging_dir, ec);
                log::error_fmt("skill '{}' already exists in '{}' scope, remove it first", skill_name, other_scope);
                return container::String(Json{{"success", false},
                            {"error", "Skill '" + skill_name + "' already exists in '" + other_scope + "' scope. Remove it first."}}
                    .dump().c_str());
            }

            auto dest_dir = target_base / skill_name;
            if (std::filesystem::exists(dest_dir)) {
                std::filesystem::remove_all(dest_dir, ec);
            }
            std::filesystem::create_directories(dest_dir.parent_path(), ec);
            log::info_fmt("copying skill: {} -> {}", skill_src.string(), dest_dir.string());
            std::filesystem::copy(skill_src, dest_dir,
                                  std::filesystem::copy_options::recursive |
                                  std::filesystem::copy_options::overwrite_existing,
                                  ec);
            if (ec) {
                std::filesystem::remove_all(temp_dir, ec);
                if (is_zip) std::filesystem::remove_all(staging_dir, ec);
                log::error_fmt("copy failed: {}", ec.message());
                return container::String(Json{{"success", false}, {"error", "Copy failed: " + ec.message()}}.dump().c_str());
            }

            auto installed_def = SkillDefinition::from_file(dest_dir / "SKILL.md", base::container::String(scope.c_str()));
            if (installed_def) {
                auto sentinel = dest_dir / ".disabled";
                if (std::filesystem::exists(sentinel)) {
                    installed_def->enabled = false;
                }
                loader->add_skill(*installed_def);
                log::info_fmt("installed skill '{}' to {}", skill_name, dest_dir.string());
            }

            std::filesystem::remove_all(temp_dir, ec);
            if (is_zip && !staging_dir.empty()) {
                std::filesystem::remove_all(staging_dir, ec);
            }

            return container::String(Json{{"success", true},
                        {"name", skill_name},
                        {"path", dest_dir.string()},
                        {"scope", scope}}.dump().c_str());
        }
    );

    // ── remove_skill ───────────────────────────────────────
    registry.register_tool(
        base::container::String("remove_skill"),
        base::container::String("Remove an installed skill by name. Deletes the skill directory from disk."),
        {
            {base::container::String("name"), ToolParameterSchema{
                .type = base::container::String("string"),
                .description = base::container::String("Skill name to remove")
            }},
            {base::container::String("scope"), ToolParameterSchema{
                .type = base::container::String("string"),
                .description = base::container::String("Scope to remove from: 'project' (default) or 'global'. If empty, removes the currently active one.")
            }}
        },
        [loader](const Json& args) -> container::String {
            std::string name = args["name"].get<std::string>();
            std::string scope = args.value("scope", "");

            log::info_fmt("remove_skill: name='{}' scope='{}'", name, scope);

            if (!loader->has_skill(name)) {
                log::warn_fmt("skill not found for removal: {}", name);
                return container::String(Json{{"success", false}, {"error", "Skill not found: " + name}}.dump().c_str());
            }

            std::filesystem::path dir_to_remove;
            if (!scope.empty()) {
                dir_to_remove = loader->target_dir(scope) / name;
            } else {
                auto project_dir = loader->project_dir() / name;
                auto global_dir = loader->global_dir() / name;
                if (std::filesystem::exists(project_dir / "SKILL.md")) {
                    dir_to_remove = project_dir;
                } else if (std::filesystem::exists(global_dir / "SKILL.md")) {
                    dir_to_remove = global_dir;
                }
            }

            if (dir_to_remove.empty() || !std::filesystem::exists(dir_to_remove)) {
                log::warn_fmt("skill directory not found on disk: {}", name);
                loader->remove_skill(name);
                return container::String(Json{{"success", true}, {"name", name}, {"note", "Removed from memory only (dir not found)"}}.dump().c_str());
            }

            std::error_code ec;
            std::filesystem::remove_all(dir_to_remove, ec);
            if (ec) {
                log::error_fmt("failed to remove skill directory: {}", ec.message());
                return container::String(Json{{"success", false}, {"error", "Failed to remove directory: " + ec.message()}}.dump().c_str());
            }

            loader->remove_skill(name);
            log::info_fmt("removed skill '{}': {}", name, dir_to_remove.string());

            return container::String(Json{{"success", true}, {"name", name}, {"path", dir_to_remove.string()}}.dump().c_str());
        }
    );

    // ── enable_skill ───────────────────────────────────────
    registry.register_tool(
        base::container::String("enable_skill"),
        base::container::String("Enable a disabled skill. Removes the .disabled marker and makes the skill available."),
        {
            {base::container::String("name"), ToolParameterSchema{
                .type = base::container::String("string"),
                .description = base::container::String("Skill name to enable")
            }}
        },
        [loader](const Json& args) -> container::String {
            std::string name = args["name"].get<std::string>();
            if (!loader->enable_skill(name)) {
                return container::String(Json{{"success", false}, {"error", "Skill not found: " + name}}.dump().c_str());
            }
            return container::String(Json{{"success", true}, {"name", name}, {"enabled", true}}.dump().c_str());
        }
    );

    // ── disable_skill ──────────────────────────────────────
    registry.register_tool(
        base::container::String("disable_skill"),
        base::container::String("Disable a skill. Writes a .disabled marker and hides the skill from the agent."),
        {
            {base::container::String("name"), ToolParameterSchema{
                .type = base::container::String("string"),
                .description = base::container::String("Skill name to disable")
            }}
        },
        [loader](const Json& args) -> container::String {
            std::string name = args["name"].get<std::string>();
            if (!loader->disable_skill(name)) {
                return container::String(Json{{"success", false}, {"error", "Skill not found: " + name}}.dump().c_str());
            }
            // 未来：unregister skill-specific tools
            return container::String(Json{{"success", true}, {"name", name}, {"enabled", false}}.dump().c_str());
        }
    );

    // ── list_skills ────────────────────────────────────────
    registry.register_tool(
        base::container::String("list_skills"),
        base::container::String("List all discovered skills with their status, version, and installation path."),
        {},
        [loader](const Json& /*args*/) -> container::String {
            auto skills = loader->skills();
            Json arr = Json::array();
            for (const auto& [name, skill] : skills) {
                arr.push_back({
                    {"name", name},
                    {"description", skill.description},
                    {"version", skill.version},
                    {"tier", skill.tier},
                    {"enabled", skill.enabled},
                    {"path", skill.skill_dir.string()}
                });
            }
            return container::String(arr.dump().c_str());
        }
    );

    log::info_fmt("registered skill management tools: install_skill, remove_skill, enable_skill, disable_skill, list_skills");
}

/// 注册所有工具的总入口（内置工具 + 技能工具 + 技能管理工具）
inline void register_all_tools(ToolRegistry& registry, int command_timeout, SkillLoader* loader) {
    register_builtin_tools(registry, command_timeout);
    if (loader) {
        register_skill_tools(registry, loader);
        register_skill_management_tools(registry, loader, &registry);
    }
}

}  // namespace ben_gear::tools
