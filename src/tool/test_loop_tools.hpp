#pragma once

#include "application/command_descriptor_factory.hpp"
#include "application/request_context.hpp"
#include "capabilities/test_loop/test_loop_service.hpp"
#include "tool/registry.hpp"
#include "tool/command_tool_helpers.hpp"

#include <memory>
#include <string>

namespace ben_gear::tools {

void register_test_loop_tools(llm::ToolRegistry& registry,
                                     std::shared_ptr<test_loop::TestLoopService> service,
                                     application::CommandPipeline command_pipeline = application::CommandPipeline(),
                                     application::RequestContext request = {},
                                     base::container::String project_path = base::container::String());


} // namespace ben_gear::tools
