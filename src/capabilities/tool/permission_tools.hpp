#pragma once

#include "capabilities/permission/policy_engine.hpp"
#include "capabilities/tool/registry.hpp"

#include <memory>

namespace ben_gear::tools {

void register_permission_tools(llm::ToolRegistry& registry,
                                      std::shared_ptr<permission::PolicyEngine> engine);


} // namespace ben_gear::tools
