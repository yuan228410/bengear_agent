#pragma once

#include "application/patch_use_cases.hpp"
#include "capabilities/patch/patch_service.hpp"
#include "capabilities/tool/registry.hpp"

#include <memory>
#include <string>

namespace ben_gear::tools {

namespace container = base::container;

namespace detail {

Json app_error_to_json(const domain::AppError& error);


container::String json_tool_output(const Json& json);


} // namespace detail

void register_patch_tools(llm::ToolRegistry& registry,
                                 std::shared_ptr<patch::PatchService> service,
                                 std::shared_ptr<application::PatchUseCases> use_cases,
                                 application::RequestContext request);


} // namespace ben_gear::tools
