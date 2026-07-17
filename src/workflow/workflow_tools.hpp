#pragma once

#include "capabilities/tool/registry.hpp"

#include <memory>
#include <string>


namespace ben_gear::workflow {

class WorkflowEngine;
class WorkflowTemplateLibrary;

using namespace ben_gear::capabilities::tool;
/// 注册工作流工具（需要引擎和模板库，由 Runtime::init_http_workflow 调用）
void register_workflow_tools(ToolRegistry& registry,
    std::shared_ptr<WorkflowEngine> engine = nullptr,
    std::shared_ptr<WorkflowTemplateLibrary> templates = nullptr);

/// 注册工作流工具（直接传入引擎和模板库，避免循环依赖）
void register_workflow_tools_with_resources(
    ToolRegistry& registry,
    std::shared_ptr<WorkflowEngine> engine,
    std::shared_ptr<WorkflowTemplateLibrary> templates);

}  // namespace ben_gear::workflow
