#pragma once

#include "intelligence/code_intel/code_intel_service.hpp"
#include "capabilities/tool/registry.hpp"
#include "capabilities/tool/command_tool_helpers.hpp"

#include <memory>

namespace ben_gear::tools {

code_intel::CodeIntelOptions code_intel_options_from_args(const Json& args);


code_intel::CodeIntelQuery code_intel_query_from_args(const Json& args);


void register_code_intel_tools(llm::ToolRegistry& registry,
                                      std::shared_ptr<code_intel::CodeIntelService> service);


} // namespace ben_gear::tools
