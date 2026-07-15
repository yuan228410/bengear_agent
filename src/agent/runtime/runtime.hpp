#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <filesystem>
#include <chrono>

#include "base/config/settings.hpp"
#include "base/concurrency/thread_pool.hpp"
#include "domain/errors.hpp"
#include "base/utils/json.hpp"

#include "llm/provider_client.hpp"
#include "capabilities/mcp/mcp_client.hpp"
#include "capabilities/skill/skill.hpp"
#include "llm/stream.hpp"
#include "capabilities/tool/registry.hpp"
#include "capabilities/tool/types.hpp"
#include "capabilities/tool/manager.hpp"

#include "memory/store.hpp"
#include "memory/context.hpp"

#include "workspace/types.hpp"
#include "workspace/history_db.hpp"
#include "workspace/manager.hpp"
#include "workspace/session.hpp"

#include "capabilities/permission/policy_engine.hpp"
#include "capabilities/patch/patch_service.hpp"
#include "capabilities/git/git_service.hpp"
#include "capabilities/checkpoint/checkpoint_service.hpp"
#include "capabilities/test_loop/test_loop_service.hpp"

// 前向声明：application 层类型仅用于 shared_ptr，完整类型在 .cpp 引入
// 避免 runtime（编排层）直接依赖 application（用例层）头文件
namespace ben_gear::application {
class WorkspaceResolver;
class PatchUseCases;
}  // namespace ben_gear::application

#include "application/command_pipeline.hpp"
#include "application/request_context.hpp"

#include "intelligence/workspace_index/workspace_index_service.hpp"
#include "intelligence/repo_map/repo_map_service.hpp"
#include "intelligence/code_intel/code_intel_service.hpp"
#include "intelligence/diagnostic_context/diagnostic_context_service.hpp"
#include "intelligence/diagnostic_repair/diagnostic_repair_plan_service.hpp"
#include "intelligence/diagnostic_repair/diagnostic_repair_patch_preview_service.hpp"

#include "workflow/workflow_engine.hpp"
#include "workflow/workflow_templates.hpp"

#include "base/net/io_context.hpp"
#include "base/net/event_loop.hpp"

#include "plugins/plugin_loader.hpp"

#include "orchestration/plan.hpp"

#include "agent/core/interface/agent_core.hpp"
#include "agent/core/interface/event_sink.hpp"
#include "agent/runtime/service_bundles.hpp"

namespace ben_gear::agent::runtime {

using Json = ben_gear::Json;

/// Agent 运行时 — 汇聚全部服务
///
/// 设计原则：
/// - 非 God Object：每个服务独立访问，不提供 "万能 API"
/// - 可按需创建：只初始化需要的服务
/// - 所有 const 方法线程安全
class Runtime : public std::enable_shared_from_this<Runtime>,
                public permission::ToolPermissionProvider {
public:
    explicit Runtime(config::Settings settings,
                     workspace::WorkspaceContext ws_ctx);
    ~Runtime();

    // 不允许拷贝
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    // ─── 延迟初始化（构造完成后调用）────────────────────────────
    void post_init();

    // ─── 核心服务访问器 ──────────────────────────────────────────

    const config::Settings& settings() const noexcept { return settings_; }
    llm::ProviderClient& provider() noexcept { return provider_; }
    const llm::ToolRegistry& tools() const noexcept { return tools_; }
    llm::ToolRegistry& tools_mut() noexcept { return tools_; }

    const std::shared_ptr<memory::MemoryStore>& memory_store() const noexcept { return memory_store_; }
    const std::unique_ptr<memory::ContextBuilder>& context_builder() const noexcept { return context_builder_; }
    workspace::HistoryDB& history_db() noexcept;
    const std::shared_ptr<workspace::WorkspaceManager>& workspace_manager() const noexcept { return ws_manager_; }

    const std::shared_ptr<mcp::MCPManager>& mcp_manager() const noexcept { return mcp_manager_; }

    /// 最小核心 Agent — 处理结构化指令（file:/http:/exec: 等），非 LLM 路径
    core::Agent& agent() noexcept { return agent_; }
    const core::Agent& agent() const noexcept { return agent_; }

    const workspace::WorkspaceContext& workspace_context() const noexcept { return ws_ctx_; }

    const std::shared_ptr<base::concurrency::ThreadPool>& core_pool() const noexcept { return infra_.core_pool; }
    const std::shared_ptr<workflow::WorkflowEngine>& workflow_engine() const noexcept { return workflow_engine_; }
    const std::shared_ptr<net::IoContext>& io_context() const noexcept { return infra_.io_context; }
    const std::shared_ptr<net::IoContext>& wf_context() const noexcept { return infra_.wf_context; }
    const std::shared_ptr<net::IoContext>& util_context() const noexcept { return infra_.util_context; }

    const std::shared_ptr<permission::PolicyEngine>& policy_engine() const noexcept { return safe_change_.policy_engine; }
    const std::shared_ptr<patch::PatchService>& patch_service() const noexcept { return safe_change_.patch_service; }
    const std::shared_ptr<git::GitService>& git_service() const noexcept { return safe_change_.git_service; }
    const std::shared_ptr<checkpoint::CheckpointService>& checkpoint_service() const noexcept { return safe_change_.checkpoint_service; }
    const std::shared_ptr<test_loop::TestLoopService>& test_loop_service() const noexcept { return safe_change_.test_loop_service; }

    const std::shared_ptr<workspace_index::WorkspaceIndexService>& workspace_index_service() const noexcept { return intelligence_.workspace_index; }
    const std::shared_ptr<repo_map::RepoMapService>& repo_map_service() const noexcept { return intelligence_.repo_map; }
    const std::shared_ptr<code_intel::CodeIntelService>& code_intel_service() const noexcept { return intelligence_.code_intel; }
    const std::shared_ptr<diagnostic_context::DiagnosticContextService>& diagnostic_context_service() const noexcept { return intelligence_.diagnostic_context; }
    const std::shared_ptr<diagnostic_repair::DiagnosticRepairPlanService>& diagnostic_repair_plan_service() const noexcept { return intelligence_.diagnostic_repair_plan; }
    const std::shared_ptr<diagnostic_repair::DiagnosticRepairPatchPreviewService>& diagnostic_repair_patch_preview_service() const noexcept { return intelligence_.diagnostic_repair_preview; }

    const std::shared_ptr<workflow::WorkflowTemplateLibrary>& template_lib() const noexcept { return template_lib_; }

    // ─── 权限检查（ToolPermissionProvider）─────────────────────
    permission::PermissionDecision evaluate_tool_permission(
        std::string_view tool_name, const Json& arguments) const override;
    Json before_tool_execution(std::string_view tool_name,
                               const Json& arguments) const override;

    // ─── 工作流资源 ──────────────────────────────────────────────
    workflow::WorkflowResources make_workflow_resources();

    // ─── Session 依赖 ────────────────────────────────────────────
    workspace::SessionDeps make_session_deps() const;

    // ─── 工具注册 ────────────────────────────────────────────────
    void register_tool(const std::string& name,
                       const std::string& description,
                       const std::vector<std::pair<std::string, llm::ToolParameterSchema>>& parameters,
                       llm::ToolExecutor executor);

    // ─── 计划管理器 ──────────────────────────────────────────────
    orchestration::PlanManager& plan_manager() noexcept { return plan_manager_; }
    const orchestration::PlanManager& plan_manager() const noexcept { return plan_manager_; }

    // ─── 异步聊天配置 ────────────────────────────────────────────
    struct SessionRunConfig {
        net::EventLoop& loop;
        workspace::Session& session;
        std::string prompt;
        const agent::AgentEventSink& event_sink;
        net::CancellationToken cancel;
        const llm::ToolRegistry* tool_override = nullptr;
    };

    // ─── 异步聊天 ────────────────────────────────────────────────
    net::Task<llm::ChatResult> run_session_async(SessionRunConfig config);
    net::Task<llm::ChatResult> run_session_async(net::EventLoop& loop,
                                                  workspace::Session& session,
                                                  std::string prompt,
                                                  const agent::AgentEventSink& event_sink,
                                                  const net::CancellationToken& cancel = {},
                                                   const llm::ToolRegistry* tool_override = nullptr);
    const skill::SkillLoader& skill_loader() const noexcept { return skill_loader_; }
    class SubAgentRuntime;
    const std::shared_ptr<SubAgentRuntime>& sub_agent_runtime() const noexcept { return sub_agent_runtime_; }

    // ─── 会话工厂 ───────────────────────────────────────────────
    /// 创建 Session（自动处理恢复/新建/持久化 sessions 表）
    std::unique_ptr<workspace::Session> make_session(
        std::string session_id);

    // ─── 最大工具限制 ───────────────────────────────────────────
    int max_tool_steps() const noexcept { return max_tool_steps_; }
    int max_tool_calls() const noexcept { return max_tool_calls_; }
    int max_tool_calls_per_step() const noexcept { return max_tool_calls_per_step_; }

private:
    // ─── 初始化（5 阶段分组）───────────────────────────────────────
    void init_all();
    void init_infrastructure();
    void init_memory_system();
    void init_tool_system();
    void init_orchestration();
    void inject_agent_defaults();

    // 细粒度初始化方法
    void init_http_workflow();
    void init_workspace();
    void init_memory();
    void ensure_default_memory_files();
    void init_history();
    void init_tools();
    void init_skills();
    void init_mcp();
    void init_workflow();
    void init_sub_agent();
    void init_plugins();
    void register_plugin_tool(const plugins::BenGearTool& tool);

    application::CommandPipeline make_command_pipeline() const;
    Json check_command_permission(std::string_view tool_name,
                                  const Json& arguments) const;
    domain::AppResult<void> create_command_checkpoint(
        const application::CommandDescriptor& command) const;
    Json append_command_audit(const std::string& workspace,
                              const std::string& session_id,
                              const std::string& username,
                              const std::string& category,
                              const std::string& action,
                              const Json& details) const;
    Json append_runtime_execution(const std::string& workspace,
                                  const std::string& session_id,
                                  const std::string& username,
                                  const Json& execution) const;

    std::string session_id_for_sub_agent() const;
    std::string normalize_checkpoint_path(const std::string& input) const;
    std::vector<std::string> checkpoint_paths_for_tool(
        std::string_view tool_name, const Json& arguments) const;
    bool tool_uses_command_pipeline(std::string_view tool_name) const;
    application::RequestContext request_context() const;
    std::shared_ptr<application::WorkspaceResolver> make_workspace_resolver() const;

    // ─── 状态 ────────────────────────────────────────────────────
    config::Settings settings_;
    llm::ProviderClient provider_;
    llm::ToolRegistry tools_;
    workspace::WorkspaceContext ws_ctx_;

    // 按职责分组的服务束（详见 service_bundles.hpp）
    SafeChangeServices safe_change_;
    IntelligenceServices intelligence_;
    InfrastructureServices infra_;

    std::shared_ptr<memory::MemoryStore> memory_store_;
    std::unique_ptr<memory::ContextBuilder> context_builder_;
    std::unique_ptr<workspace::HistoryDB> history_db_;
    std::shared_ptr<workspace::WorkspaceManager> ws_manager_;

    std::shared_ptr<mcp::MCPManager> mcp_manager_;
    std::shared_ptr<workflow::WorkflowEngine> workflow_engine_;
    std::shared_ptr<workflow::WorkflowTemplateLibrary> template_lib_;
    skill::SkillLoader skill_loader_;
    orchestration::PlanManager plan_manager_;
    std::shared_ptr<SubAgentRuntime> sub_agent_runtime_;

    std::unique_ptr<plugins::PluginLoader> plugin_loader_;

    core::Agent agent_;

    int max_tool_steps_;
    int max_tool_calls_;
    int max_tool_calls_per_step_;
    bool post_initialized_ = false;
};

/// 子 Agent 运行时
///
/// 创建并行子代理执行独立任务，结果自动聚合。
/// 通过 delegate_to_sub_agent 工具让 LLM 自主委派。
class Runtime::SubAgentRuntime {
public:
    explicit SubAgentRuntime(const config::Settings& settings,
                             llm::ProviderClient& provider,
                             const llm::ToolRegistry& tools);

    /// 停止后台 EventLoop，等待线程结束
    ~SubAgentRuntime();

    /// 设置父代理事件回调
    void set_parent_event_sink(std::shared_ptr<domain::EventSink> sink) { parent_sink_ = std::move(sink); }

    /// 执行单个子代理（同步，在调用线程中运行）
    struct Result {
        bool success = false;
        std::string output;
        int tool_calls = 0;
        std::chrono::milliseconds duration{0};
    };

    Result execute(net::EventLoop& loop,
                   std::string_view prompt,
                   const agent::SubAgentConfig& config);

    /// 并行执行多个子代理，返回结果数组（调用线程同步等全部完成）
    std::vector<Result> execute_parallel(
        net::EventLoop& loop,
        const std::vector<std::string>& prompts,
        const agent::SubAgentConfig& config,
        int max_parallel);

    const agent::SubAgentConfig& default_config() const { return default_config_; }

    /// 获取后台 EventLoop（供外部直接调度）
    net::EventLoop& loop() noexcept { return sub_loop_; }

private:
    void execute_locked(net::EventLoop& loop, std::string_view prompt,
                        const agent::SubAgentConfig& config, Result& result);
    const agent::SubAgentConfig default_config_;
    config::Settings settings_;
    llm::ProviderClient& provider_;
    const llm::ToolRegistry& tools_;
    std::shared_ptr<domain::EventSink> parent_sink_;
    std::mutex provider_mutex_;

    /// 后台 EventLoop：创建一次，所有子代理调用复用（避免每次创建线程开销）
    net::EventLoop sub_loop_;
    std::thread loop_thread_;
};

} // namespace ben_gear::agent::runtime
