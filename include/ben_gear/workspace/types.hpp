#pragma once

#include "ben_gear/base/container/string.hpp"

#include <filesystem>
#include <string>

namespace ben_gear::workspace {

namespace container = base::container;

/// 三层级标识
enum class Tier { global, user, workspace };

/// 三层级路径集合
struct TierPaths {
    std::filesystem::path global_dir;     // ~/.bengear/
    std::filesystem::path user_dir;       // ~/.bengear/users/<username>/
    std::filesystem::path workspace_dir;  // ~/.bengear/users/<username>/workspaces/<ws>/

    /// 按层级返回目录
    const std::filesystem::path& dir(Tier tier) const {
        switch (tier) {
            case Tier::global: return global_dir;
            case Tier::user: return user_dir;
            case Tier::workspace: return workspace_dir;
        }
        return global_dir;
    }

    /// 层级名转枚举
    static Tier tier_from_name(std::string_view name) {
        if (name == "global") return Tier::global;
        if (name == "user") return Tier::user;
        return Tier::workspace;
    }

    /// 枚举转层级名
    static const char* tier_name(Tier tier) {
        switch (tier) {
            case Tier::global: return "global";
            case Tier::user: return "user";
            case Tier::workspace: return "workspace";
        }
        return "workspace";
    }
};

/// 工作空间元数据
struct WorkspaceMeta {
    container::String name;
    container::String project_path;   // 关联的项目路径
    std::filesystem::path ws_dir;     // 工作空间数据目录
    bool deleted = false;             // 软删除标记
};

/// 会话元数据
struct SessionMeta {
    container::String session_id;     // UUID v4
    container::String workspace_name;
    container::String name;           // 可选的会话名称
    std::filesystem::path session_dir;
    std::string created_at;           // ISO 8601
    std::string updated_at;
};

/// 工作空间上下文（传递给 Agent / Session）
struct WorkspaceContext {
    TierPaths tier_paths;
    container::String workspace_name;
    container::String username;
    container::String session_id;     // 当前活跃会话，空=新建
};

}  // namespace ben_gear::workspace
