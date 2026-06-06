#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/vector.hpp"
#include "ben_gear/base/log/logger.hpp"
#include "ben_gear/base/utils/json.hpp"
#include "ben_gear/base/platform/platform.hpp"
#include "ben_gear/workspace/types.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <optional>
#include <vector>

namespace ben_gear::workspace {

namespace container = base::container;

/// 工作空间管理器（CRUD + 软删除/恢复 + 初始化模板）
class WorkspaceManager {
public:
    explicit WorkspaceManager(const std::filesystem::path& user_dir)
        : user_dir_(user_dir),
          workspaces_dir_(user_dir / "workspaces") {
        std::filesystem::create_directories(workspaces_dir_);
        ensure_default();
    }

    /// 创建工作空间
    std::optional<WorkspaceMeta> create(
        const container::String& name,
        const container::String& project_path = {}) {
        auto name_str = std::string(name.data(), name.size());
        if (!is_valid_workspace_name(name_str)) {
            log::error_fmt("invalid workspace name: {}", name_str);
            return std::nullopt;
        }
        auto dir = workspaces_dir_ / name_str;
        if (std::filesystem::exists(dir)) {
            log::warn_fmt("workspace already exists: {}", name_str);
            return std::nullopt;
        }
        return create_workspace_dir(name, project_path);
    }

    /// 获取工作空间
    std::optional<WorkspaceMeta> get(const container::String& name) const {
        auto name_str = std::string(name.data(), name.size());
        auto dir = workspaces_dir_ / name_str;
        if (!std::filesystem::exists(dir)) return std::nullopt;
        return load_meta(name, dir);
    }

    /// 列出所有工作空间
    container::Vector<WorkspaceMeta> list_all() const {
        container::Vector<WorkspaceMeta> result;
        if (!std::filesystem::exists(workspaces_dir_)) return result;

        for (const auto& entry : std::filesystem::directory_iterator(workspaces_dir_)) {
            if (!entry.is_directory()) continue;
            auto name = entry.path().filename().string();
            // 跳过软删除的
            if (name.starts_with(".")) continue;
            auto meta = load_meta(container::String(name.c_str()), entry.path());
            if (meta) result.push_back(*meta);
        }
        return result;
    }

    /// 列出已删除的工作空间
    container::Vector<WorkspaceMeta> list_removed() const {
        container::Vector<WorkspaceMeta> result;
        if (!std::filesystem::exists(workspaces_dir_)) return result;

        for (const auto& entry : std::filesystem::directory_iterator(workspaces_dir_)) {
            if (!entry.is_directory()) continue;
            auto dirname = entry.path().filename().string();
            if (!dirname.starts_with(".")) continue;
            if (dirname.find(".removed_") == std::string::npos) continue;

            // 从 ".<name>.removed_<ts>" 中提取原始名称
            auto name = dirname.substr(1); // 去掉前导 "."
            auto removed_pos = name.find(".removed_");
            if (removed_pos != std::string::npos) {
                name = name.substr(0, removed_pos);
            }

            // 尝试从 workspace.json 读取完整元数据
            auto meta = load_meta(container::String(name.c_str()), entry.path());
            if (meta) {
                meta->deleted = true;
                result.push_back(*meta);
            } else {
                WorkspaceMeta fallback;
                fallback.name = container::String(name.c_str());
                fallback.ws_dir = entry.path();
                fallback.deleted = true;
                result.push_back(fallback);
            }
        }
        return result;
    }

    /// 软删除工作空间（重命名为 .<name>.removed_<timestamp>）
    bool remove(const container::String& name) {
        auto name_str = std::string(name.data(), name.size());
        if (name_str == "default") {
            log::warn_fmt("cannot remove default workspace");
            return false;
        }

        auto dir = workspaces_dir_ / name_str;
        if (!std::filesystem::exists(dir)) return false;

        auto now = std::chrono::system_clock::now();
        auto ts = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()).count();
        auto removed_name = "." + name_str + ".removed_" + std::to_string(ts);
        auto removed_dir = workspaces_dir_ / removed_name;

        std::error_code ec;
        std::filesystem::rename(dir, removed_dir, ec);
        if (ec) {
            log::error_fmt("workspace remove failed: {}, error={}", name_str, ec.message());
            return false;
        }

        log::info_fmt("workspace removed: {} -> {}", name_str, removed_name);
        return true;
    }

    /// 恢复已删除的工作空间
    bool restore(const container::String& name) {
        auto name_str = std::string(name.data(), name.size());

        // 查找匹配的 .removed 目录
        if (!std::filesystem::exists(workspaces_dir_)) return false;

        for (const auto& entry : std::filesystem::directory_iterator(workspaces_dir_)) {
            if (!entry.is_directory()) continue;
            auto dirname = entry.path().filename().string();
            // 匹配 .<name>.removed_<timestamp>
            auto prefix = "." + name_str + ".removed_";
            if (!dirname.starts_with(prefix)) continue;

            auto target = workspaces_dir_ / name_str;
            std::error_code ec;
            std::filesystem::rename(entry.path(), target, ec);
            if (ec) {
                log::error_fmt("workspace restore failed: {}, error={}", name_str, ec.message());
                return false;
            }

            log::info_fmt("workspace restored: {} -> {}", dirname, name_str);
            return true;
        }
        return false;
    }

    /// 获取工作空间的 TierPaths
    TierPaths tier_paths_for(const container::String& ws_name) const {
        auto root = support::data_directory();
        auto ws_str = std::string(ws_name.data(), ws_name.size());
        return {
            root,                        // global
            user_dir_,                   // user
            workspaces_dir_ / ws_str     // workspace
        };
    }

private:
    void ensure_default() {
        auto default_dir = workspaces_dir_ / "default";
        if (!std::filesystem::exists(default_dir)) {
            create_workspace_dir(container::String("default"), {});
            log::info_fmt("default workspace created");
        }
    }

    WorkspaceMeta create_workspace_dir(
        const container::String& name,
        const container::String& project_path) {
        auto name_str = std::string(name.data(), name.size());

        // 防止目录遍历：禁止路径分隔符和 ..
        if (name_str.find('/') != std::string::npos ||
            name_str.find('\\') != std::string::npos ||
            name_str.find("..") != std::string::npos) {
            log::error_fmt("invalid workspace name (path traversal): {}", name_str);
            return {name, project_path, {}, false};
        }

        auto dir = workspaces_dir_ / name_str;

        // 创建目录结构
        std::filesystem::create_directories(dir);
        std::filesystem::create_directories(dir / "memory_data");
        std::filesystem::create_directories(dir / "memory_data" / "sessions");
        std::filesystem::create_directories(dir / "skills");
        std::filesystem::create_directories(dir / "roles");
        std::filesystem::create_directories(dir / ".team" / "inbox");

        // 写入 workspace.json
        {
            auto meta_path = dir / "workspace.json";
            Json meta;
            meta["name"] = name_str;
            meta["project_path"] = std::string(project_path.data(), project_path.size());
            std::ofstream file(meta_path, std::ios::binary | std::ios::trunc);
            file << meta.dump(2);
        }

        // 写入默认模板
        write_default_templates(dir);

        log::info_fmt("workspace created: {}", name_str);
        return {name, project_path, dir, false};
    }

    void write_default_templates(const std::filesystem::path& dir) {
        // 默认 SOUL.md
        {
            auto path = dir / "memory_data" / "SOUL.md";
            if (!std::filesystem::exists(path)) {
                std::ofstream file(path, std::ios::binary);
                file << "You are BenGear, a concise cross-platform coding agent.\n";
            }
        }

        // 默认 RULES.md（空）
        {
            auto path = dir / "memory_data" / "RULES.md";
            if (!std::filesystem::exists(path)) {
                std::ofstream file(path, std::ios::binary);
            }
        }

        // 默认 MEMORY.md（空）
        {
            auto path = dir / "memory_data" / "MEMORY.md";
            if (!std::filesystem::exists(path)) {
                std::ofstream file(path, std::ios::binary);
            }
        }

        // 默认 lead.json
        {
            auto path = dir / "roles" / "lead.json";
            if (!std::filesystem::exists(path)) {
                std::ofstream file(path, std::ios::binary);
                file << R"({"name": "lead", "description": "Full access agent", "tool_whitelist": []})";
            }
        }

        // 默认 teammate.json
        {
            auto path = dir / "roles" / "teammate.json";
            if (!std::filesystem::exists(path)) {
                std::ofstream file(path, std::ios::binary);
                file << R"({"name": "teammate", "description": "Restricted agent for collaboration", "tool_whitelist": ["read_file", "list_dir", "run_command", "http_get", "get_skill"]})";
            }
        }
    }

    std::optional<WorkspaceMeta> load_meta(
        const container::String& name,
        const std::filesystem::path& dir) const {
        auto meta_path = dir / "workspace.json";
        if (std::filesystem::exists(meta_path)) {
            std::ifstream file(meta_path, std::ios::binary);
            if (file) {
                std::string content{
                    std::istreambuf_iterator<char>(file),
                    std::istreambuf_iterator<char>()};
                std::string err;
                auto json = parse_json(content, err);
                if (err.empty()) {
                    return WorkspaceMeta{
                        container::String(json.value("name", "").c_str()),
                        container::String(json.value("project_path", "").c_str()),
                        dir,
                        false
                    };
                }
            }
        }
        return WorkspaceMeta{name, {}, dir, false};
    }

    std::filesystem::path user_dir_;
    std::filesystem::path workspaces_dir_;

    static bool is_valid_workspace_name(std::string_view name) {
        if (name.empty() || name.size() > 128) return false;
        for (char c : name) {
            if (c == '/' || c == '\\' || c == '.' || c == '\0' || c == ':') return false;
        }
        if (name == "..") return false;
        return true;
    }
};

}  // namespace ben_gear::workspace
