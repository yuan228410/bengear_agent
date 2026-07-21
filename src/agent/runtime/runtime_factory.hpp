#pragma once

#include <memory>
#include <string>
#include <vector>

#include "config/settings.hpp"
#include "workspace/types.hpp"
#include "workflow/workflow_engine.hpp"

namespace ben_gear::memory { class MemoryStore; }
namespace ben_gear::workspace { struct WorkspaceContext; }
namespace ben_gear::plugins { struct BenGearTool; }

namespace ben_gear::agent::runtime {

class Runtime;

/// Runtime 工厂 — 负责创建和初始化 Runtime 实例
class RuntimeFactory {
public:
    static std::shared_ptr<Runtime> create(
        config::Settings settings,
        workspace::WorkspaceContext ws_ctx);

    static std::shared_ptr<Runtime> create_uninitialized(
        config::Settings settings,
        workspace::WorkspaceContext ws_ctx);

    static void initialize(Runtime& runtime);

private:
    RuntimeFactory() = delete;

    // 初始化子系统
    static void init_infrastructure(Runtime& runtime);
    static void init_memory_system(Runtime& runtime);
    static void init_tool_system(Runtime& runtime);
    static void init_orchestration(Runtime& runtime);

    // 细粒度初始化
    static void init_http_workflow(Runtime& runtime);
    static void init_workspace(Runtime& runtime);
    static void init_memory(Runtime& runtime);
    static void ensure_default_memory_files(Runtime& runtime,
                                            memory::MemoryStore& store,
                                            const workspace::WorkspaceContext& ws_ctx);
    static void init_history(Runtime& runtime);
    static void init_tools(Runtime& runtime);
    static void init_skills(Runtime& runtime);
    static void init_mcp(Runtime& runtime);
    static void init_workflow(Runtime& runtime);
    static void init_sub_agent(Runtime& runtime);
    static void init_plugins(Runtime& runtime);
    static void init_capabilities(Runtime& runtime);

    // 工具注册
    static void register_plugin_tool(Runtime& runtime, const plugins::BenGearTool& tool);

    // 辅助方法
    static workflow::WorkflowResources make_workflow_resources_for(Runtime& rt);
};

} // namespace ben_gear::agent::runtime
