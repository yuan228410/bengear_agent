#include "capabilities/skill/skill_tools.hpp"

#include "capabilities/skill/skill.hpp"
#include "capabilities/skill/zip_extract.hpp"
#include "capabilities/tool/registry.hpp"
#include "log/logger.hpp"
#include "net/io_context.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace ben_gear::skill {

using namespace ben_gear::capabilities::tool;
using SkillDefinition = ben_gear::skill::SkillDefinition;
using SkillLoader = ben_gear::skill::SkillLoader;
using skill::download_file;
using skill::extract_zip;

/// 注册 get_skill 工具（Level 2 按需加载）
void register_skill_tools(ToolRegistry& registry, SkillLoader* loader) {
    if (!loader) return;

    registry.register_tool(
        std::string("get_skill"),
        std::string("Load a skill's full content by name. Use this when you need detailed instructions for a skill."),
        {
            {std::string("name"), ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("Skill name to load")
            }}
        },
        [loader](const Json& args) -> std::string {
            auto name = args.value("name", "");
            auto content = loader->get_skill_content(name);
            log::info_fmt("get_skill: name={} content_len={}", name, content.size());
            return content;
        }
    );

    log::info_fmt("registered skill tools: get_skill");
}

/// 生成临时目录路径
std::string make_temp_dir() {
    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::random_device rd;
    return "/tmp/bengear_skill_" + std::to_string(now) + "_" + std::to_string(rd());
}

/// 注册技能管理工具（install, remove, enable, disable, list）
void register_skill_management_tools(ToolRegistry& registry,
                                             SkillLoader* loader,
                                             net::IoContext& io_ctx,
                                             net::TlsEngine& tls_engine,
                                             compress::CompressEngine& compress_engine) {
    if (!loader) return;

    // ── install_skill ──────────────────────────────────────
    registry.register_tool(
        std::string("install_skill"),
        std::string("Install a skill from a remote zip URL, local zip file, or local directory. "
                          "Scope 'workspace' (default) installs to workspace, 'user' to user-level, 'global' to global."),
        {
            {std::string("source"), ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("Source: remote zip URL (https://...), local zip path, or local directory path")
            }},
            {std::string("scope"), ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("Installation scope: 'workspace' (default), 'user', or 'global'")
            }}
        },
        [loader, &io_ctx, &tls_engine, &compress_engine](const Json& args) -> std::string {
            std::string source = args.at("source").get<std::string>();
            std::string scope = args.value("scope", "workspace");

            log::info_fmt("install_skill: source='{}' scope='{}'", source, scope);

            auto target_base = loader->target_dir(scope);
            std::error_code ec;
            std::filesystem::create_directories(target_base, ec);
            if (ec) {
                log::error_fmt("failed to create target dir: {}", ec.message());
                return Json{{"success", false}, {"error", "Failed to create target directory: " + ec.message()}}.dump();
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
                if (!download_file(source, zip_path, io_ctx, tls_engine, /*expect_zip=*/true)) {
                    std::filesystem::remove_all(temp_dir, ec);
                    return Json{{"success", false}, {"error", "Download failed: " + source}}.dump();
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
                if (!extract_zip(zip_path, staging_dir, compress_engine)) {
                    std::filesystem::remove_all(temp_dir, ec);
                    std::filesystem::remove_all(staging_dir, ec);
                    return Json{{"success", false}, {"error", "Zip extraction failed: " + zip_path}}.dump();
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
                    return Json{{"success", false}, {"error", "No SKILL.md found in source"}}.dump();
                }
            }

            auto def = SkillDefinition::from_file(skill_src / "SKILL.md", scope);
            if (!def) {
                std::filesystem::remove_all(temp_dir, ec);
                if (is_zip) std::filesystem::remove_all(staging_dir, ec);
                log::error_fmt("failed to parse SKILL.md: {}", (skill_src / "SKILL.md").string());
                return Json{{"success", false}, {"error", "Failed to parse SKILL.md"}}.dump();
            }

            std::string skill_name = std::string(def->name);

            // 检查其他 tier 是否已有同名 skill（三层级互斥）
            for (const auto& other : {"global", "user", "workspace"}) {
                if (other == scope) continue;
                if (loader->has_skill_in_scope(skill_name, other)) {
                    std::filesystem::remove_all(temp_dir, ec);
                    if (is_zip) std::filesystem::remove_all(staging_dir, ec);
                    log::error_fmt("skill '{}' already exists in '{}' scope, remove it first", skill_name, other);
                    return std::string(Json{{"success", false},
                                {"error", "Skill '" + skill_name + "' already exists in '" + std::string(other) + "' scope. Remove it first."}}
                        .dump().c_str());
                }
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
                return Json{{"success", false}, {"error", "Copy failed: " + ec.message()}}.dump();
            }

            auto installed_def = SkillDefinition::from_file(dest_dir / "SKILL.md", scope);
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

            return std::string(Json{{"success", true},
                        {"name", skill_name},
                        {"path", dest_dir.string()},
                        {"scope", scope}}.dump().c_str());
        }
    );

    // ── remove_skill ───────────────────────────────────────
    registry.register_tool(
        std::string("remove_skill"),
        std::string("Remove an installed skill by name. Deletes the skill directory from disk."),
        {
            {std::string("name"), ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("Skill name to remove")
            }},
            {std::string("scope"), ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("Scope to remove from: 'workspace' (default), 'user', or 'global'. If empty, removes the currently active one.")
            }}
        },
        [loader](const Json& args) -> std::string {
            std::string name = args.at("name").get<std::string>();
            std::string scope = args.value("scope", "");

            log::info_fmt("remove_skill: name='{}' scope='{}'", name, scope);

            if (!loader->has_skill(name)) {
                log::warn_fmt("skill not found for removal: {}", name);
                return Json{{"success", false}, {"error", "Skill not found: " + name}}.dump();
            }

            std::filesystem::path dir_to_remove;
            if (!scope.empty()) {
                dir_to_remove = loader->target_dir(scope) / name;
            } else {
                // 三层级 fallback：workspace → user → global
                auto project_dir = loader->project_dir() / name;
                auto user_dir = loader->user_dir() / name;
                auto global_dir = loader->global_dir() / name;
                if (std::filesystem::exists(project_dir / "SKILL.md")) {
                    dir_to_remove = project_dir;
                } else if (std::filesystem::exists(user_dir / "SKILL.md")) {
                    dir_to_remove = user_dir;
                } else if (std::filesystem::exists(global_dir / "SKILL.md")) {
                    dir_to_remove = global_dir;
                }
            }

            if (dir_to_remove.empty() || !std::filesystem::exists(dir_to_remove)) {
                log::warn_fmt("skill directory not found on disk: {}", name);
                loader->remove_skill(name);
                return Json{{"success", true}, {"name", name}, {"note", "Removed from memory only (dir not found)"}}.dump();
            }

            std::error_code ec;
            std::filesystem::remove_all(dir_to_remove, ec);
            if (ec) {
                log::error_fmt("failed to remove skill directory: {}", ec.message());
                return Json{{"success", false}, {"error", "Failed to remove directory: " + ec.message()}}.dump();
            }

            loader->remove_skill(name);
            log::info_fmt("removed skill '{}': {}", name, dir_to_remove.string());

            return Json{{"success", true}, {"name", name}, {"path", dir_to_remove.string()}}.dump();
        }
    );

    // ── enable_skill ───────────────────────────────────────
    registry.register_tool(
        std::string("enable_skill"),
        std::string("Enable a disabled skill. Removes the .disabled marker and makes the skill available."),
        {
            {std::string("name"), ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("Skill name to enable")
            }}
        },
        [loader](const Json& args) -> std::string {
            std::string name = args.at("name").get<std::string>();
            if (!loader->enable_skill(name)) {
                return Json{{"success", false}, {"error", "Skill not found: " + name}}.dump();
            }
            return Json{{"success", true}, {"name", name}, {"enabled", true}}.dump();
        }
    );

    // ── disable_skill ──────────────────────────────────────
    registry.register_tool(
        std::string("disable_skill"),
        std::string("Disable a skill. Writes a .disabled marker and hides the skill from the agent."),
        {
            {std::string("name"), ToolParameterSchema{
                .type = std::string("string"),
                .description = std::string("Skill name to disable")
            }}
        },
        [loader](const Json& args) -> std::string {
            std::string name = args.at("name").get<std::string>();
            if (!loader->disable_skill(name)) {
                return Json{{"success", false}, {"error", "Skill not found: " + name}}.dump();
            }
            // 未来：unregister skill-specific tools
            return Json{{"success", true}, {"name", name}, {"enabled", false}}.dump();
        }
    );

    // ── list_skills ────────────────────────────────────────
    registry.register_tool(
        std::string("list_skills"),
        std::string("List all discovered skills with their status, version, and installation path."),
        {},
        [loader](const Json& /*args*/) -> std::string {
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
            return arr.dump();
        }
    );

    log::info_fmt("registered skill management tools: install_skill, remove_skill, enable_skill, disable_skill, list_skills");
}

/// 注册技能工具的总入口（技能工具 + 技能管理工具）
/// 内置工具由运行时单独注册
void register_all_tools(ToolRegistry& registry, int /*command_timeout*/,
                        SkillLoader* loader, net::IoContext& io_ctx,
                        net::TlsEngine& tls_engine,
                        compress::CompressEngine& compress_engine) {
    if (loader) {
        register_skill_tools(registry, loader);
        register_skill_management_tools(registry, loader, io_ctx, tls_engine, compress_engine);
    }
}

}  // namespace ben_gear::skill
