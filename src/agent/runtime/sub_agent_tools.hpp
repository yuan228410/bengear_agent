#pragma once

#include <memory>
#include <string>

#include "capabilities/tool/registry.hpp"

namespace ben_gear::agent::runtime { class SubAgentRuntime; }

namespace ben_gear::tools {

void register_sub_agent_tools(
    capabilities::tool::ToolRegistry& registry,
    std::shared_ptr<agent::runtime::SubAgentRuntime> runtime);

/// 从目录发现并注册自定义子 Agent（.md 文件，frontmatter: name, description，正文为 system_prompt）
/// 每个文件注册为 sub_<name> 工具
void register_custom_sub_agents(
    capabilities::tool::ToolRegistry& registry,
    std::shared_ptr<agent::runtime::SubAgentRuntime> runtime,
    const std::string& directory);

/// 注册子代理管理工具：sub_create / sub_update / sub_remove / sub_list
void register_sub_agent_management_tools(
    capabilities::tool::ToolRegistry& registry,
    std::shared_ptr<agent::runtime::SubAgentRuntime> runtime,
    const std::string& sub_agents_dir);

} // namespace ben_gear::tools
