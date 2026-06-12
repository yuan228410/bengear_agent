#pragma once

#include "ben_gear/agent/sub_agent.hpp"
#include "ben_gear/tool/registry.hpp"

#include <memory>

namespace ben_gear::tools {

/// 注册子 Agent 工具（delegate_task / delegate_tasks）
/// 实现在 sub_agent_tools.cpp 中（避免头文件循环依赖）
void register_sub_agent_tools(
    llm::ToolRegistry& registry,
    std::shared_ptr<agent::SubAgentRuntime> runtime);

} // namespace ben_gear::tools
