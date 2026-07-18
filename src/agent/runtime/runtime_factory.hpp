#pragma once

#include <memory>
#include <string>
#include <vector>

#include "base/config/settings.hpp"
#include "workspace/types.hpp"

namespace ben_gear::plugins { struct BenGearTool; }

namespace ben_gear::agent::runtime {

class Runtime;

/// Runtime 工厂 — 负责创建和初始化 Runtime 实例
///
/// 将 16 个 init 方法从 Runtime 中分离，职责单一。
/// Runtime 本身只保留服务访问和生命周期管理。
class RuntimeFactory {
public:
    /// 创建并初始化 Runtime
    /// @param settings 配置
    /// @param ws_ctx   工作空间上下文
    /// @return 初始化完成的 Runtime 实例
    static std::shared_ptr<Runtime> create(
        config::Settings settings,
        workspace::WorkspaceContext ws_ctx);

    /// 创建未初始化的 Runtime（用于测试或延迟初始化）
    static std::shared_ptr<Runtime> create_uninitialized(
        config::Settings settings,
        workspace::WorkspaceContext ws_ctx);

    /// 初始化已有的 Runtime 实例
    static void initialize(Runtime& runtime);

private:
    RuntimeFactory() = delete;

    // 初始化子系统（按依赖顺序）
    static void init_infrastructure(Runtime& runtime);
    static void init_memory_system(Runtime& runtime);
    static void init_tool_system(Runtime& runtime);
    static void init_orchestration(Runtime& runtime);
    static void inject_agent_defaults(Runtime& runtime);

    // 细粒度初始化
    static void init_http_workflow(Runtime& runtime);
    static void init_workspace(Runtime& runtime);
    static void init_memory(Runtime& runtime);
    static void ensure_default_memory_files(Runtime& runtime);
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
};

} // namespace ben_gear::agent::runtime
