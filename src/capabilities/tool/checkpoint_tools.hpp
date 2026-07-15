#pragma once

#include "application/command_descriptor_factory.hpp"
#include "application/request_context.hpp"
#include "capabilities/checkpoint/checkpoint_service.hpp"
#include "capabilities/tool/registry.hpp"
#include "capabilities/tool/command_tool_helpers.hpp"

#include <memory>
#include <vector>

namespace ben_gear::tools {

void register_checkpoint_tools(llm::ToolRegistry& registry,
                                      std::shared_ptr<checkpoint::CheckpointService> service,
                                      application::CommandPipeline command_pipeline = application::CommandPipeline(),
                                      application::RequestContext request = {},
                                      std::string project_path = std::string());


} // namespace ben_gear::tools
