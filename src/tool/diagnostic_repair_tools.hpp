#pragma once

#include "intelligence/diagnostic_repair/diagnostic_repair_patch_preview_service.hpp"
#include "intelligence/diagnostic_repair/diagnostic_repair_plan_service.hpp"
#include "intelligence/diagnostic_repair/diagnostic_repair_workflow_service.hpp"
#include "tool/registry.hpp"
#include "tool/command_tool_helpers.hpp"

#include <memory>

namespace ben_gear::tools {

auto diagnostic_repair_parameters(bool include_patch_preview);


auto diagnostic_repair_workflow_parameters();


void register_diagnostic_repair_tools(
    llm::ToolRegistry& registry,
    std::shared_ptr<diagnostic_repair::DiagnosticRepairPlanService> service,
    std::shared_ptr<diagnostic_repair::DiagnosticRepairPatchPreviewService> patch_preview_service = nullptr,
    std::shared_ptr<diagnostic_repair::DiagnosticRepairWorkflowService> workflow_service = nullptr);


} // namespace ben_gear::tools
