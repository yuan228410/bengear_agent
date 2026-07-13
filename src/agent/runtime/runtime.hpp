#pragma once

#include <memory>
#include <string>
#include <vector>
#include <filesystem>
#include <chrono>

#include "base/config/settings.hpp"
#include "base/concurrency/thread_pool.hpp"
#include "domain/errors.hpp"
#include "base/utils/json.hpp"

#include "llm/provider_client.hpp"
#include "llm/mcp/mcp_client.hpp"
#include "llm/skill/skill.hpp"
#include "llm/stream.hpp"
#include "tool/registry.hpp"
#include "tool/types.hpp"
#include "tool/manager.hpp"

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

#include "application/workspace_resolver.hpp"
#include "application/patch_use_cases.hpp"
#include "application/command_governance.hpp"

#include "intelligence/workspace_index/workspace_index_service.hpp"
#include "intelligence/repo_map/repo_map_service.hpp"
#include "intelligence/code_intel/code_intel_service.hpp"
#include "intelligence/diagnostic_context/diagnostic_context_service.hpp"
#include "intelligence/diagnostic_repair/diagnostic_repair_plan_service.hpp"
#include "intelligence/diagnostic_repair/diagnostic_repair_patch_preview_service.hpp"

#include "workflow/workflow_engine.hpp"
#include "workflow/workflow_templates.hpp"

#include "net/io_context.hpp"
#include "net/event_loop.hpp"

#include "plugins/plugin_loader.hpp"

#include "orchestration/plan.hpp"

#include "agent/core/interface/agent_core.hpp"
#include "agent/core/interface/event_sink.hpp"

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

    const std::shared_ptr<base::concurrency::ThreadPool>& core_pool() const noexcept { return core_pool_; }
    const std::shared_ptr<workflow::WorkflowEngine>& workflow_engine() const noexcept { return workflow_engine_; }
    const std::shared_ptr<net::IoContext>& io_context() const noexcept { return io_context_; }
    const std::shared_ptr<net::IoContext>& wf_context() const noexcept { return wf_context_; }
    const std::shared_ptr<net::IoContext>& util_context() const noexcept { return util_context_; }

    const std::shared_ptr<permission::PolicyEngine>& policy_engine() const noexcept { return policy_engine_; }
    const std::shared_ptr<patch::PatchService>& patch_service() const noexcept { return patch_service_; }
    const std::shared_ptr<git::GitService>& git_service() const noexcept { return git_service_; }
    const std::shared_ptr<checkpoint::CheckpointService>& checkpoint_service() const noexcept { return checkpoint_service_; }
    const std::shared_ptr<test_loop::TestLoopService>& test_loop_service() const noexcept { return test_loop_service_; }

    const std::shared_ptr<workspace_index::WorkspaceIndexService>& workspace_index_service() const noexcept { return workspace_index_service_; }
    const std::shared_ptr<repo_map::RepoMapService>& repo_map_service() const noexcept { return repo_map_service_; }
    const std::shared_ptr<code_intel::CodeIntelService>& code_intel_service() const noexcept { return code_intel_service_; }
    const std::shared_ptr<diagnostic_context::DiagnosticContextService>& diagnostic_context_service() const noexcept { return diagnostic_context_service_; }
    const std::shared_ptr<diagnostic_repair::DiagnosticRepairPlanService>& diagnostic_repair_plan_service() const noexcept { return diagnostic_repair_plan_service_; }
    const std::shared_ptr<diagnostic_repair::DiagnosticRepairPatchPreviewService>& diagnostic_repair_patch_preview_service() const noexcept { return diagnostic_repair_patch_preview_service_; }

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
    void register_tool(const container::String& name,
                       const container::String& description,
                       const container::Vector<std::pair<container::String, llm::ToolParameterSchema>>& parameters,
                       llm::ToolExecutor executor);

    // ─── 计划管理器 ──────────────────────────────────────────────
    orchestration::PlanManager& plan_manager() noexcept { return plan_manager_; }
    const orchestration::PlanManager& plan_manager() const noexcept { return plan_manager_; }

    // ─── 异步聊天 ────────────────────────────────────────────────
    net::Task<llm::ChatResult> run_session_async(net::EventLoop& loop,
                                                  workspace::Session& session,
                                                  base::container::String prompt,
                                                  const agent::AgentEventSink& event_sink,
                                                  const net::CancellationToken& cancel = {},
                                                   const llm::ToolRegistry* tool_override = nullptr);
    const skill::SkillLoader& skill_loader() const noexcept { return skill_loader_; }
    class SubAgentRuntime;
    const std::shared_ptr<SubAgentRuntime>& sub_agent_runtime() const noexcept { return sub_agent_runtime_; }

    // ─── 最大工具限制 ───────────────────────────────────────────
    int max_tool_steps() const noexcept { return max_tool_steps_; }
    int max_tool_calls() const noexcept { return max_tool_calls_; }
    int max_tool_calls_per_step() const noexcept { return max_tool_calls_per_step_; }

private:
    void init_all();
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
    Json append_command_audit(const container::String& workspace,
                              const container::String& session_id,
                              const container::String& username,
                              const container::String& category,
                              const container::String& action,
                              const Json& details) const;
    Json append_runtime_execution(const container::String& workspace,
                                  const container::String& session_id,
                                  const container::String& username,
                                  const Json& execution) const;

    container::String session_id_for_sub_agent() const;
    std::string normalize_checkpoint_path(const std::string& input) const;
    std::vector<std::string> checkpoint_paths_for_tool(
        std::string_view tool_name, const Json& arguments) const;
    bool tool_uses_command_pipeline(std::string_view tool_name) const;
    application::RequestContext request_context() const;
    application::WorkspaceResolver make_workspace_resolver() const;

    // ─── 状态 ────────────────────────────────────────────────────
    config::Settings settings_;
    llm::ProviderClient provider_;
    llm::ToolRegistry tools_;
    workspace::WorkspaceContext ws_ctx_;

    std::shared_ptr<permission::PolicyEngine> policy_engine_;
    std::shared_ptr<patch::PatchService> patch_service_;
    std::shared_ptr<application::WorkspaceResolver> patch_workspace_resolver_;
    std::shared_ptr<application::PatchUseCases> patch_use_cases_;
    std::shared_ptr<git::GitService> git_service_;
    std::shared_ptr<checkpoint::CheckpointService> checkpoint_service_;
    std::shared_ptr<test_loop::TestLoopService> test_loop_service_;
    std::shared_ptr<workspace_index::WorkspaceIndexService> workspace_index_service_;
    std::shared_ptr<repo_map::RepoMapService> repo_map_service_;
    std::shared_ptr<code_intel::CodeIntelService> code_intel_service_;
    std::shared_ptr<diagnostic_context::DiagnosticContextService> diagnostic_context_service_;
    std::shared_ptr<diagnostic_repair::DiagnosticRepairPlanService> diagnostic_repair_plan_service_;
    std::shared_ptr<diagnostic_repair::DiagnosticRepairPatchPreviewService> diagnostic_repair_patch_preview_service_;

    std::shared_ptr<memory::MemoryStore> memory_store_;
    std::unique_ptr<memory::ContextBuilder> context_builder_;
    std::unique_ptr<workspace::HistoryDB> history_db_;
    std::shared_ptr<workspace::WorkspaceManager> ws_manager_;

    std::shared_ptr<mcp::MCPManager> mcp_manager_;
    std::shared_ptr<base::concurrency::ThreadPool> core_pool_;
    std::shared_ptr<net::IoContext> io_context_;
    std::shared_ptr<net::IoContext> wf_context_;
    std::shared_ptr<net::IoContext> util_context_;
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

private:
    const agent::SubAgentConfig default_config_;
    config::Settings settings_;
    llm::ProviderClient& provider_;
    const llm::ToolRegistry& tools_;
    std::shared_ptr<domain::EventSink> parent_sink_;
};

} // namespace ben_gear::agent::runtime
