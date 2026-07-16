#pragma once

#include "orchestration/plan.hpp"
#include "workflow/workflow_engine.hpp"
#include "workflow/workflow_templates.hpp"
#include <memory>

#include "plugins/plugin_loader.hpp"

namespace ben_gear::agent::runtime {

class Runtime;

/// Abstract interface for orchestration subsystem — enables mock injection for testing
struct IOrchestrationContext {
    virtual ~IOrchestrationContext() = default;
    virtual const std::shared_ptr<workflow::WorkflowEngine>& workflow() const = 0;
    virtual const std::shared_ptr<workflow::WorkflowTemplateLibrary>& templates() const = 0;
    virtual orchestration::PlanManager& plans() = 0;
    virtual const orchestration::PlanManager& plans() const = 0;
    virtual const std::unique_ptr<plugins::PluginLoader>& plugin_loader() const = 0;
};

/// Concrete orchestration subsystem: workflow, templates, plans, plugin loader
struct OrchestrationContext : IOrchestrationContext {
    std::shared_ptr<workflow::WorkflowEngine> workflow_;
    std::shared_ptr<workflow::WorkflowTemplateLibrary> templates_;
    orchestration::PlanManager plans_;
    std::unique_ptr<plugins::PluginLoader> plugin_loader_;

    OrchestrationContext() = default;
    OrchestrationContext(std::shared_ptr<workflow::WorkflowEngine> wf,
                         std::shared_ptr<workflow::WorkflowTemplateLibrary> tmpl,
                         std::unique_ptr<plugins::PluginLoader> pl = {})
        : workflow_(std::move(wf)), templates_(std::move(tmpl)),
          plugin_loader_(std::move(pl)) {}
    const std::shared_ptr<workflow::WorkflowEngine>& workflow() const override { return workflow_; }
    const std::shared_ptr<workflow::WorkflowTemplateLibrary>& templates() const override { return templates_; }
    orchestration::PlanManager& plans() override { return plans_; }
    const orchestration::PlanManager& plans() const override { return plans_; }
    const std::unique_ptr<plugins::PluginLoader>& plugin_loader() const override { return plugin_loader_; }
};

} // namespace ben_gear::agent::runtime
