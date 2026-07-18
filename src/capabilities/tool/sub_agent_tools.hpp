#pragma once

#include <memory>

#include "capabilities/tool/registry.hpp"

namespace ben_gear::agent::runtime { class SubAgentRuntime; }

namespace ben_gear::tools {

void register_sub_agent_tools(
    capabilities::tool::ToolRegistry& registry,
    std::shared_ptr<agent::runtime::SubAgentRuntime> runtime);

} // namespace ben_gear::tools
