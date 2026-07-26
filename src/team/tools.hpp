#pragma once

#include "capabilities/tool/registry.hpp"

#include <memory>
#include <string>

namespace ben_gear::team {

class TeamOrchestrator;

/// 注册团队相关的 LLM 工具
/// @param registry 工具注册表
/// @param orchestrator 团队编排器（由 Runtime 创建，通过 ServiceRegistry 传入）
void register_team_tools(
    capabilities::tool::ToolRegistry& registry,
    std::shared_ptr<TeamOrchestrator> orchestrator);

} // namespace ben_gear::team
