#pragma once

#include <filesystem>
#include <string_view>

namespace ben_gear::base {

/// 三层级标识（global / user / workspace），优先级递增
enum class Tier { global, user, workspace };

/// 三层级路径集合
struct TierPaths {
    std::filesystem::path global_dir;     // ~/.bengear/
    std::filesystem::path user_dir;        // ~/.bengear/users/<name>/（用户级配置）
    std::filesystem::path workspace_dir;   // ~/.bengear/users/<name>/workspaces/<ws>/（工作空间级）

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

}  // namespace ben_gear::base
