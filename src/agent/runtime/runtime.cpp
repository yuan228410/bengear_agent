#include "agent/runtime/runtime.hpp"

#include <cstring>
#include <filesystem>

#include "compress/compress_engine.hpp"
#include "log/logger.hpp"
#include "concurrency/thread_pool.hpp"
#include "net/event_loop.hpp"
#include "net/io_context.hpp"
#include "platform/platform.hpp"
#include "workspace/session.hpp"

#include "llm/provider_client.hpp"
#include "capabilities/tool/registry.hpp"
#include "capabilities/mcp/mcp_client.hpp"
#include "capabilities/skill/skill.hpp"
#include "capabilities/capability_registry.hpp"

#include "memory/store.hpp"
#include "memory/context.hpp"

#include "workspace/history_db.hpp"
#include "workspace/manager.hpp"

#include "workflow/workflow_engine.hpp"
#include "workflow/workflow_templates.hpp"

#include "plugins/plugin_loader.hpp"

#include "orchestration/plan.hpp"

#include "agent/core/interfaces.hpp"
#include "agent/core/events.hpp"
#include "agent/core/event_sink.hpp"
#include "agent/runtime/service_bundles.hpp"
#include "agent/runtime/tool_context.hpp"
#include "agent/runtime/memory_context.hpp"
#include "agent/runtime/orchestration_context.hpp"
#include "agent/runtime/sub_agent_runtime.hpp"
#include "agent/execution/loop.hpp"
#include "agent/execution/interceptor.hpp"
#include "agent/execution/service_interface.hpp"
#include "agent/execution/timeout_policy.hpp"
#include "agent/execution/interceptors/plan_interceptor.hpp"
#include "agent/execution/interceptors/compaction_interceptor.hpp"

namespace ben_gear::agent::runtime {

// ─── InternalServices — 实际持有全部子服务实例 ──────────────────
//
// 利用 Opaque Pointer (PIMPL) 模式，不在头文件中暴露具体服务类型。
// 所有服务通过 ServiceRegistry 按类型索引访问，编译隔离好。

struct Runtime::InternalServices {
    // 基础设施
    InfrastructureServices infra;

    // TLS 引擎（必须早于 provider 创建）
    std::unique_ptr<net::TlsEngine> tls_engine;
    // 压缩引擎
    std::unique_ptr<compress::CompressEngine> compress_engine;

    // 核心服务
    llm::ProviderClient provider;
    ToolContext tools;
    MemoryContext memory;
    OrchestrationContext orch;

    // 可选服务
    std::shared_ptr<SubAgentRuntime> sub_agent;
    skill::SkillLoader skill_loader;
    std::vector<std::unique_ptr<capabilities::ICapability>> capabilities;
    // 五大服务接口（默认实现，可通过 ServiceRegistry 替换）
    std::shared_ptr<core::IFileService>       file_svc;
    std::shared_ptr<core::IWebAccessService>   web_svc;
    std::shared_ptr<core::ISkillService>       skill_svc;
    std::shared_ptr<core::ICommandExecutor>    cmd_svc;
    std::shared_ptr<core::IMCPService>         mcp_svc;

    InternalServices(config::Settings& settings, workspace::WorkspaceContext& ws_ctx)
        : infra{
              std::make_shared<base::concurrency::ThreadPool>(
                  base::concurrency::to_thread_pool_config(settings.thread_pool)),
              std::make_shared<net::IoContext>("io"),
              std::make_shared<net::IoContext>("workflow"),
              std::make_shared<net::IoContext>("util"),
          },
          tls_engine(net::create_default_tls_engine()),
          compress_engine(compress::create_default_compress_engine()),
          provider(settings, *tls_engine),
          tools(settings.mcp.read_buffer_size, *tls_engine),
          orch{
              std::make_shared<workflow::WorkflowEngine>(
                  workflow::WorkflowResources{}, nullptr),
              std::make_shared<workflow::WorkflowTemplateLibrary>(),
          },
          skill_loader(skill::make_skill_loader(ws_ctx.tier_paths)) {}
};

// ═══════════════════════════════════════════════════════════════════
//  Runtime 实现
// ═══════════════════════════════════════════════════════════════════

Runtime::Runtime(config::Settings settings, workspace::WorkspaceContext ws_ctx)
    : settings_(std::move(settings)),
      ws_ctx_(std::move(ws_ctx)),
      max_tool_steps_(settings_.agent.max_tool_steps),
      max_tool_calls_(settings_.agent.max_tool_calls),
      max_tool_calls_per_step_(settings_.agent.max_tool_calls_per_step),
      max_parallel_tools_(settings_.agent.max_parallel_tools) {
    // InternalServices 延迟到 init_internals() 中创建，
    // 允许 RuntimeBuilder 在创建前预注入服务
}

void Runtime::init_internals() {
    if (internal_) return;
    internal_ = std::make_unique<InternalServices>(settings_, ws_ctx_);
    register_services();
}

Runtime::~Runtime() {
    if (lifecycle_.is_ready()) {
        shutdown();
    }
}

void Runtime::register_services() {
    auto& svc = services_;

    // 辅助：已注册则跳过，否则注册
    auto reg_if_absent = [&svc]<typename T>(T* ptr) {
        if (!svc.template resolve<T>()) {
            svc.register_service<T>(ptr);
        }
    };

    // 基础设施（始终注册，不会被覆盖）
    svc.register_service<config::Settings>(&settings_);
    svc.register_service<workspace::WorkspaceContext>(&ws_ctx_);
    svc.register_service<base::concurrency::ThreadPool>(internal_->infra.core_pool.get());
    svc.register_service<InfrastructureServices>(&internal_->infra);
    svc.register_service<net::IoContext>(internal_->infra.io_context.get());
    svc.register_service<net::TlsEngine>(internal_->tls_engine.get());
    svc.register_service<compress::CompressEngine>(internal_->compress_engine.get());

    // 核心服务
    svc.register_service<llm::ProviderClient>(&internal_->provider);
    svc.register_service<capabilities::tool::ToolRegistry>(&internal_->tools.registry_);
    svc.register_service<mcp::MCPManager>(internal_->tools.mcp_.get());

    // Facade 接口（支持测试 Mock）
    svc.register_service<IToolContext>(&internal_->tools);
    svc.register_service<IMemoryContext>(&internal_->memory);
    svc.register_service<IOrchestrationContext>(&internal_->orch);

    // 五大服务接口 — 若已预注入则跳过，否则创建默认实现
    if (!svc.resolve<core::IFileService>()) {
        internal_->file_svc = core::make_default_file_service();
        svc.register_service<core::IFileService>(internal_->file_svc.get());
    }
    if (!svc.resolve<core::IWebAccessService>()) {
        internal_->web_svc = core::make_default_web_service();
        svc.register_service<core::IWebAccessService>(internal_->web_svc.get());
    }
    if (!svc.resolve<core::ISkillService>()) {
        internal_->skill_svc = core::make_default_skill_service();
        svc.register_service<core::ISkillService>(internal_->skill_svc.get());
    }
    if (!svc.resolve<core::ICommandExecutor>()) {
        internal_->cmd_svc = core::make_default_command_executor();
        svc.register_service<core::ICommandExecutor>(internal_->cmd_svc.get());
    }
    if (!svc.resolve<core::IMCPService>()) {
        internal_->mcp_svc = core::make_default_mcp_service();
        svc.register_service<core::IMCPService>(internal_->mcp_svc.get());
    }

    // ─── 工作流 ────────────────────────────────────────────────
    svc.register_service<workflow::WorkflowEngine>(internal_->orch.workflow_.get());
    svc.register_service<workflow::WorkflowTemplateLibrary>(internal_->orch.templates_.get());
    svc.register_service<orchestration::PlanManager>(&internal_->orch.plans_);

    // ─── 事件总线 ──────────────────────────────────────────────
    svc.register_service<base::EventBus>(&event_bus_);

    // ─── 可观测性（默认空实现，可替换） ─────────────────────────
    svc.register_service<base::IMetricsCollector>(&metrics_);
    svc.register_service<base::ITracer>(&tracer_);

    // 自动订阅 EventBus 指标（当 EventBus 有事件时记录计数器）
    event_bus_.subscribe<agent::TokenEvent>(
        [this](const auto&) { metrics_.counter("llm.tokens", 1, {}); });
    event_bus_.subscribe<agent::ToolCallEvent>(
        [this](const auto&) { metrics_.counter("tool.calls", 1, {}); });
    event_bus_.subscribe<agent::ToolResultEvent>(
        [this](const auto&) { metrics_.counter("tool.results", 1, {}); });
    event_bus_.subscribe<agent::SubAgentStartEvent>(
        [this](const auto&) { metrics_.counter("sub_agent.starts", 1, {}); });
}

void Runtime::shutdown() {
    if (!lifecycle_.is_ready()) return;
    lifecycle_.begin_shutdown();

    // 逆序关闭：先关高层服务，再关底层基础设施
    internal_->sub_agent.reset();
    internal_->orch.plugin_loader_.reset();
    internal_->orch.workflow_.reset();
    internal_->tools.mcp_.reset();
    internal_->memory.history_db_.reset();

    if (internal_->infra.io_context) internal_->infra.io_context->drain();
    if (internal_->infra.wf_context) internal_->infra.wf_context->drain();
    if (internal_->infra.util_context) internal_->infra.util_context->drain();
    if (internal_->infra.core_pool) internal_->infra.core_pool->shutdown();

    services_.clear();
    lifecycle_.end_shutdown();
}

std::unique_ptr<workspace::Session> Runtime::make_session(std::string session_id) {
    auto& settings = settings_;
    auto& ws_ctx = ws_ctx_;

    workspace::SessionDeps deps{
        .ws_ctx = ws_ctx,
        .memory_store = internal_->memory.store_,
        .context_builder = internal_->memory.builder_.get(),
        .thread_pool = internal_->infra.core_pool
    };

    auto session = std::make_unique<workspace::Session>(
        workspace::SessionConfig{
            session_id, settings.llm.context_length, settings.context_prune,
            config::SessionType::main, {}
        },
        deps, internal_->tools.registry_);

    if (!session_id.empty() && internal_->memory.history_db_) {
        session->restore_from_db(*internal_->memory.history_db_);
    } else if (internal_->memory.history_db_) {
        auto ws_name = ws_ctx.workspace_name.empty()
            ? std::string("default") : ws_ctx.workspace_name;
        internal_->memory.history_db_->create_session(
            ws_ctx.username, ws_name, session->session_id(),
            std::string(), config::SessionType::main);
    }

    return session;
}

} // namespace ben_gear::agent::runtime
