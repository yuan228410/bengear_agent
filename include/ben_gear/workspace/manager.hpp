#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/vector.hpp"
#include "ben_gear/base/utils/json.hpp"
#include "ben_gear/base/platform/platform.hpp"
#include "ben_gear/workspace/types.hpp"

#include <filesystem>
#include <optional>

namespace ben_gear::workspace {

namespace container = base::container;

/// 工作空间管理器（CRUD + 软删除/恢复 + 初始化模板）
class WorkspaceManager {
public:
    explicit WorkspaceManager(const std::filesystem::path& user_dir);

    /// 创建工作空间
    std::optional<WorkspaceMeta> create(
        const container::String& name,
        const container::String& project_path = {});

    /// 获取工作空间
    std::optional<WorkspaceMeta> get(const container::String& name) const;

    /// 列出所有工作空间
    container::Vector<WorkspaceMeta> list_all() const;

    /// 列出已删除的工作空间
    container::Vector<WorkspaceMeta> list_removed() const;

    /// 软删除工作空间（重命名为 .<name>.removed_<timestamp>）
    bool remove(const container::String& name);

    /// 恢复已删除的工作空间
    bool restore(const container::String& name);

    /// 获取工作空间的 TierPaths
    TierPaths tier_paths_for(const container::String& ws_name) const;

private:
    void ensure_default();

    WorkspaceMeta create_workspace_dir(
        const container::String& name,
        const container::String& project_path);

    std::optional<WorkspaceMeta> load_meta(
        const container::String& name,
        const std::filesystem::path& dir) const;

    std::filesystem::path user_dir_;
    std::filesystem::path workspaces_dir_;

    static bool is_valid_workspace_name(std::string_view name);
};

}  // namespace ben_gear::workspace
