#pragma once

#include "intelligence/diagnostic_context/diagnostic_context_service.hpp"
#include "tool/registry.hpp"
#include "tool/command_tool_helpers.hpp"

#include <memory>

namespace ben_gear::tools {

void register_diagnostic_context_tools(llm::ToolRegistry& registry,
                                              std::shared_ptr<diagnostic_context::DiagnosticContextService> service);


} // namespace ben_gear::tools
