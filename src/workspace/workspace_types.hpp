#pragma once

#include "base/tier_paths.hpp"
#include <filesystem>
#include <string>

namespace ben_gear::workspace {

using Tier = base::Tier;
using TierPaths = base::TierPaths;

/// 工作空间元数据
struct WorkspaceMeta {
    std::string name;
    std::string project_path;
    std::filesystem::path ws_dir;
    bool deleted = false;
};

/// 工作空间上下文（纯数据，无重量依赖）
struct WorkspaceContext {
    TierPaths tier_paths;
    std::string workspace_name;
    std::string project_path;
    std::string username;
    std::string session_id = {};
};

} // namespace ben_gear::workspace
