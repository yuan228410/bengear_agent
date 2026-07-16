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

#include "application/command_pipeline.hpp"
#include "application/request_context.hpp"
#include "application/workspace_resolver.hpp"

#include "workflow/workflow_engine.hpp"
#include "workflow/workflow_templates.hpp"

#include "base/net/io_context.hpp"
#include "base/net/event_loop.hpp"

#include "plugins/plugin_loader.hpp"

#include "orchestration/plan.hpp"
#include "capabilities/capability_registry.hpp"

#include "agent/core/interface/agent_core.hpp"
#include "agent/core/interface/event_sink.hpp"
#include "agent/runtime/service_bundles.hpp"

namespace ben_gear::agent::runtime {

using Json = ben_gear::Json;

/// Agent 运行时 — 汇聚全部服务
class Runtime : public std::enable_shared_from_this<Runtime> {
public:
    explicit Runtime(config::Settings settings,
                     workspace::WorkspaceContext ws_ctx);
    ~Runtime();

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    void post_init();

    const config::Settings& settings() const noexcept { return settings_; }
    llm::ProviderClient& provider() noexcept { return provider_; }
    const capabilities::tool::ToolRegistry& tools() const noexcept { return tools_; }
    capabilities::tool::ToolRegistry& tools_mut() noexcept { return tools_; }

    const std::shared_ptr<memory::MemoryStore>& memory_store() const noexcept { return memory_store_; }
    const std::unique_ptr<memory::ContextBuilder>& context_builder() const noexcept { return context_builder_; }
    workspace::HistoryDB& history_db() noexcept;
    const std::shared_ptr<workspace::WorkspaceManager>& workspace_manager() const noexcept { return ws_manager_; }

    const std::shared_ptr<mcp::MCPManager>& mcp_manager() const noexcept { return mcp_manager_; }

    core::Agent& agent() noexcept { return agent_; }
    const core::Agent& agent() const noexcept { return agent_; }

    const workspace::WorkspaceContext& workspace_context() const noexcept { return ws_ctx_; }

    const std::shared_ptr<base::concurrency::ThreadPool>& core_pool() const noexcept { return infra_.core_pool; }
    const std::shared_ptr<workflow::WorkflowEngine>& workflow_engine() const noexcept { return workflow_engine_; }
    const std::shared_ptr<net::IoContext>& io_context() const noexcept { return infra_.io_context; }
    const std::shared_ptr<net::IoContext>& wf_context() const noexcept { return infra_.wf_context; }
    const std::shared_ptr<net::IoContext>& util_context() const noexcept { return infra_.util_context; }

    const std::shared_ptr<workflow::WorkflowTemplateLibrary>& template_lib() const noexcept { return template_lib_; }

    workspace::SessionDeps make_session_deps() const;

    void register_tool(const std::string& name,
                       const std::string& description,
                       const std::vector<std::pair<std::string, capabilities::tool::ToolParameterSchema>>& parameters,
                       capabilities::tool::ToolExecutor executor);

    orchestration::PlanManager& plan_manager() noexcept { return plan_manager_; }
    const orchestration::PlanManager& plan_manager() const noexcept { return plan_manager_; }

    struct SessionRunConfig {
        net::EventLoop& loop;
        workspace::Session& session;
        std::string prompt;
        agent::AgentEventSinks event_sink;
        net::CancellationToken cancel;
        const capabilities::tool::ToolRegistry* tool_override = nullptr;
    };

    net::Task<llm::ChatResult> run_session_async(SessionRunConfig config);
    net::Task<llm::ChatResult> run_session_async(net::EventLoop& loop,
                                                  workspace::Session& session,
                                                  std::string prompt,
                                                  const agent::AgentEventSinks& event_sink,
                                                  const net::CancellationToken& cancel = {},
                                                   const capabilities::tool::ToolRegistry* tool_override = nullptr);
    const skill::SkillLoader& skill_loader() const noexcept { return skill_loader_; }
    class SubAgentRuntime;
    const std::shared_ptr<SubAgentRuntime>& sub_agent_runtime() const noexcept { return sub_agent_runtime_; }

    std::unique_ptr<workspace::Session> make_session(
        std::string session_id);

    int max_tool_steps() const noexcept { return max_tool_steps_; }
    int max_tool_calls() const noexcept { return max_tool_calls_; }
    int max_tool_calls_per_step() const noexcept { return max_tool_calls_per_step_; }

    workflow::WorkflowResources make_workflow_resources();

private:
    void init_all();
    void init_infrastructure();
    void init_memory_system();
    void init_tool_system();
    void init_orchestration();
    void inject_agent_defaults();

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
    void init_capabilities();
    void register_plugin_tool(const plugins::BenGearTool& tool);

    application::RequestContext request_context() const;
    std::shared_ptr<application::WorkspaceResolver> make_workspace_resolver() const;

    config::Settings settings_;
    llm::ProviderClient provider_;
    capabilities::tool::ToolRegistry tools_;
    workspace::WorkspaceContext ws_ctx_;

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
    std::vector<std::unique_ptr<capabilities::ICapability>> capabilities_;
    std::shared_ptr<SubAgentRuntime> sub_agent_runtime_;

    std::unique_ptr<plugins::PluginLoader> plugin_loader_;

    core::Agent agent_;

    int max_tool_steps_;
    int max_tool_calls_;
    int max_tool_calls_per_step_;
    bool post_initialized_ = false;
};

class Runtime::SubAgentRuntime {
public:
    explicit SubAgentRuntime(const config::Settings& settings,
                             llm::ProviderClient& provider,
                             const capabilities::tool::ToolRegistry& tools);

    ~SubAgentRuntime();

    void set_parent_event_sink(std::shared_ptr<domain::EventSink> sink) { parent_sink_ = std::move(sink); }

    struct Result {
        bool success = false;
        std::string output;
        int tool_calls = 0;
        std::chrono::milliseconds duration{0};
    };

    Result execute(net::EventLoop& loop,
                   std::string_view prompt,
                   const agent::SubAgentConfig& config);

    std::vector<Result> execute_parallel(
        net::EventLoop& loop,
        const std::vector<std::string>& prompts,
        const agent::SubAgentConfig& config,
        int max_parallel);

    const agent::SubAgentConfig& default_config() const { return default_config_; }

    net::EventLoop& loop() noexcept {
        start_loop();
        return sub_loop_;
    }

private:
    void execute_locked(net::EventLoop& loop, std::string_view prompt,
                        const agent::SubAgentConfig& config, Result& result);
    const agent::SubAgentConfig default_config_;
    config::Settings settings_;
    llm::ProviderClient& provider_;
    const capabilities::tool::ToolRegistry& tools_;
    std::shared_ptr<domain::EventSink> parent_sink_;
    std::mutex provider_mutex_;

    void start_loop();
    void stop_loop();
    net::EventLoop sub_loop_;
    std::thread loop_thread_;
    std::mutex loop_mutex_;
    bool loop_running_ = false;
};

} // namespace ben_gear::agent::runtime
