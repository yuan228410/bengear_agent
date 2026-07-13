#pragma once

#include "intelligence/repo_map/repo_map_service.hpp"
#include "tool/registry.hpp"
#include "tool/command_tool_helpers.hpp"

#include <memory>

namespace ben_gear::tools {

repo_map::RepoMapService::Options repo_map_options_from_args(const Json& args);


void register_repo_map_tools(llm::ToolRegistry& registry,
                                    std::shared_ptr<repo_map::RepoMapService> service);


} // namespace ben_gear::tools
