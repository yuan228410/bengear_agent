#pragma once

#include "application/command_descriptor_factory.hpp"
#include "application/request_context.hpp"
#include "capabilities/git/git_service.hpp"
#include "tool/registry.hpp"
#include "tool/command_tool_helpers.hpp"

#include <memory>
#include <string>
#include <vector>

namespace ben_gear::tools {

void register_git_tools(llm::ToolRegistry& registry,
                               std::shared_ptr<git::GitService> service,
                               application::CommandPipeline command_pipeline = application::CommandPipeline(),
                               application::RequestContext request = {},
                               base::container::String project_path = base::container::String());


} // namespace ben_gear::tools
