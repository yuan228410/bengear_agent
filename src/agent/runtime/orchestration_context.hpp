#pragma once

#include "orchestration/plan.hpp"
#include "workflow/workflow_engine.hpp"
#include "workflow/workflow_templates.hpp"
#include "plugins/plugin_loader.hpp"

#include <memory>

namespace ben_gear::agent::runtime {

class Runtime;

struct OrchestrationContext {
    std::shared_ptr<workflow::WorkflowEngine> workflow;
    std::shared_ptr<workflow::WorkflowTemplateLibrary> templates;
    orchestration::PlanManager plans;
    std::unique_ptr<plugins::PluginLoader> plugin_loader;
};

} // namespace ben_gear::agent::runtime
