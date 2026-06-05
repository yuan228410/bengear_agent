#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/vector.hpp"
#include "ben_gear/base/log/logger.hpp"
#include "ben_gear/base/platform/platform.hpp"
#include "ben_gear/role/types.hpp"

#include <filesystem>
#include <optional>
#include <shared_mutex>

namespace ben_gear::role {

namespace container = base::container;

/// 角色加载器（三层级扫描，线程安全）
/// 扫描 global → user → workspace 的 roles/ 目录，同名 last-wins
class RoleLoader {
public:
    RoleLoader(const std::filesystem::path& global_dir,
               const std::filesystem::path& user_dir,
               const std::filesystem::path& workspace_dir)
        : global_dir_(global_dir / "roles"),
          user_dir_(user_dir / "roles"),
          workspace_dir_(workspace_dir / "roles") {}

    /// 扫描三层级目录，加载角色定义
    void discover() {
        container::Map<container::String, RoleDefinition> discovered;

        scan_directory_into("global", global_dir_, discovered);
        scan_directory_into("user", user_dir_, discovered);
        scan_directory_into("workspace", workspace_dir_, discovered);

        {
            std::unique_lock lock(mutex_);
            roles_ = std::move(discovered);
        }

        log::info_fmt("RoleLoader discovered {} roles", roles_.size());
    }

    /// 获取指定角色
    std::optional<RoleDefinition> get_role(const container::String& name) const {
        std::shared_lock lock(mutex_);
        auto it = roles_.find(std::string_view(name.data(), name.size()));
        if (it != roles_.end()) return it->second;
        return std::nullopt;
    }

    /// 列出所有角色
    container::Vector<RoleDefinition> list_roles() const {
        std::shared_lock lock(mutex_);
        container::Vector<RoleDefinition> result;
        for (const auto& [name, def] : roles_) {
            result.push_back(def);
        }
        return result;
    }

private:
    void scan_directory_into(const char* tier,
                             const std::filesystem::path& dir,
                             container::Map<container::String, RoleDefinition>& out) {
        if (!std::filesystem::exists(dir)) return;

        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (!entry.is_regular_file()) continue;
            auto path = entry.path();
            if (path.extension() != ".json") continue;

            auto role = parse_role_file(path, tier);
            if (role) {
                auto name = role->name;
                log::debug_fmt("role loaded: name={} tier={} file={}",
                              std::string(name.data(), name.size()), tier, path.string());
                out[name] = std::move(*role);
            }
        }
    }

    std::optional<RoleDefinition> parse_role_file(
        const std::filesystem::path& path, const char* tier) const {
        std::ifstream file(path, std::ios::binary);
        if (!file) return std::nullopt;

        std::string content{
            std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>()};

        std::string err;
        auto json = parse_json(content, err);
        if (!err.empty()) {
            log::error_fmt("role parse error: file={}, error={}", path.string(), err);
            return std::nullopt;
        }

        RoleDefinition def;
        def.name = container::String(json.value("name", "").c_str());
        def.description = container::String(json.value("description", "").c_str());
        def.tier = container::String(tier);

        if (json.contains("tool_whitelist") && json["tool_whitelist"].is_array()) {
            for (const auto& tool : json["tool_whitelist"]) {
                def.tool_whitelist.push_back(container::String(tool.get<std::string>().c_str()));
            }
        }

        if (def.name.empty()) return std::nullopt;
        return def;
    }

    std::filesystem::path global_dir_;
    std::filesystem::path user_dir_;
    std::filesystem::path workspace_dir_;
    container::Map<container::String, RoleDefinition> roles_;
    mutable std::shared_mutex mutex_;
};

}  // namespace ben_gear::role
