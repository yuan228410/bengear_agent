#pragma once

#include "base/utils/json.hpp"
#include "tool/registry.hpp"
#include "workflow/workflow_engine.hpp"
#include "workflow/workflow_templates.hpp"
#include "workflow/metrics.hpp"
#include "workflow/human_approval.hpp"
#include "workflow/visualizer.hpp"
#include "base/log/logger.hpp"

#include <memory>
#include <string>

namespace ben_gear::tools {

using namespace ben_gear::llm;
using namespace ben_gear::workflow;

// 前向声明（实现在下方）
void register_workflow_tools_with_resources(
    ToolRegistry& registry,
    std::shared_ptr<WorkflowEngine> engine,
    std::shared_ptr<WorkflowTemplateLibrary> templates);

/// 注册工作流工具（需要引擎和模板库，由 Runtime::init_http_workflow 调用）
void register_workflow_tools(ToolRegistry& registry,
    std::shared_ptr<WorkflowEngine> engine = nullptr,
    std::shared_ptr<WorkflowTemplateLibrary> templates = nullptr);


/// 注册工作流工具
/// 注册工作流工具（直接传入引擎和模板库，避免循环依赖）
void register_workflow_tools_with_resources(
    ToolRegistry& registry,
    std::shared_ptr<WorkflowEngine> engine,
    std::shared_ptr<WorkflowTemplateLibrary> templates);


}  // namespace ben_gear::tools