#pragma once

#include <vector>
#include "workspace/types.hpp"

#include <filesystem>
#include <optional>

namespace ben_gear::workspace {


/// 工作空间管理器（CRUD + 软删除/恢复 + 初始化模板）
class WorkspaceManager {
public:
    explicit WorkspaceManager(const std::filesystem::path& user_dir);

    /// 创建工作空间
    std::optional<WorkspaceMeta> create(
        const std::string& name,
        const std::string& project_path = {});

    /// 获取工作空间
    std::optional<WorkspaceMeta> get(const std::string& name) const;

    /// 更新工作空间的项目路径到 workspace.json
    bool set_project_path(const std::string& name,
                          const std::filesystem::path& project_path);

    /// 列出所有工作空间
    std::vector<WorkspaceMeta> list_all() const;

    /// 列出已删除的工作空间
    std::vector<WorkspaceMeta> list_removed() const;

    /// 软删除工作空间（重命名为 .<name>.removed_<timestamp>）
    bool remove(const std::string& name);

    /// 恢复已删除的工作空间
    bool restore(const std::string& name);

    /// 获取工作空间的 TierPaths
    TierPaths tier_paths_for(const std::string& ws_name) const;

private:
    void ensure_default();

    WorkspaceMeta create_workspace_dir(
        const std::string& name,
        const std::string& project_path);

    std::optional<WorkspaceMeta> load_meta(
        const std::string& name,
        const std::filesystem::path& dir) const;

    std::filesystem::path user_dir_;
    std::filesystem::path workspaces_dir_;

    static bool is_valid_workspace_name(std::string_view name);
};

}  // namespace ben_gear::workspace
