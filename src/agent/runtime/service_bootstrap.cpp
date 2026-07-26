#include "agent/runtime/service_bootstrap.hpp"

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

#include "memory/store.hpp"
#include "memory/context.hpp"

#include "workspace/history_db.hpp"
#include "workspace/manager.hpp"

#include "plugins/plugin_loader.hpp"

#include "orchestration/plan.hpp"

#include "agent/core/interfaces.hpp"
#include "agent/core/events.hpp"
#include "agent/runtime/service_bundles.hpp"
#include "agent/runtime/tool_context.hpp"
#include "agent/runtime/memory_context.hpp"
#include "agent/runtime/orchestration_context.hpp"
#include "agent/runtime/sub_agent_runtime.hpp"

namespace ben_gear::agent::runtime {

// ─── InternalServices — 实际持有全部子服务实例 ──────────────────
struct ServiceBootstrap::InternalServices {
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
    // 核心服务接口（默认实现，可通过 ServiceRegistry 替换）
    std::shared_ptr<core::IFileService>       file_svc;
    std::shared_ptr<core::ICommandExecutor>    cmd_svc;

    InternalServices(config::Settings& settings, workspace::WorkspaceContext& ws_ctx)
        : infra{
              std::make_shared<base::concurrency::ThreadPool>(
                  base::concurrency::to_thread_pool_config(settings.thread_pool)),
              std::make_shared<net::IoContext>("io"),
              std::make_shared<net::IoContext>("util"),
          },
          tls_engine(net::create_default_tls_engine()),
          compress_engine(compress::create_default_compress_engine()),
          provider(settings, *tls_engine),
          tools(settings.mcp.read_buffer_size, *tls_engine),
          skill_loader(skill::make_skill_loader(ws_ctx.tier_paths)) {}
};

// ═══════════════════════════════════════════════════════════════════
//  ServiceBootstrap 实现
// ═══════════════════════════════════════════════════════════════════

ServiceBootstrap::ServiceBootstrap(config::Settings settings, workspace::WorkspaceContext ws_ctx)
    : settings_(std::move(settings)),
      ws_ctx_(std::move(ws_ctx)),
      max_tool_steps_(settings_.agent.max_tool_steps),
      max_tool_calls_(settings_.agent.max_tool_calls),
      max_tool_calls_per_step_(settings_.agent.max_tool_calls_per_step),
      max_parallel_tools_(settings_.agent.max_parallel_tools) {
}

ServiceBootstrap::~ServiceBootstrap() {
    event_bus_.clear();
    if (lifecycle_.is_ready()) {
        shutdown();
    }
}

MemoryContext& ServiceBootstrap::memory() noexcept {
    return internal_->memory;
}

void ServiceBootstrap::init() {
    if (internal_) return;
    internal_ = std::make_unique<InternalServices>(settings_, ws_ctx_);
    register_services();
}

void ServiceBootstrap::register_services() {
    auto& svc = services_;

    // 基础设施（始终注册，不会被覆盖）
    svc.register_service<config::Settings>(&settings_);
    svc.register_service<workspace::WorkspaceContext>(&ws_ctx_);
    svc.register_service<base::concurrency::ThreadPool>(internal_->infra.core_pool.get());
    svc.register_service<InfrastructureServices>(&internal_->infra);
    svc.register_service<net::IoContext>(internal_->infra.io_context.get());
    svc.register_service<net::TlsEngine>(internal_->tls_engine.get());
    svc.register_service<compress::CompressEngine>(internal_->compress_engine.get());

    // 核心服务（注册接口类型，支持 Mock 注入）
    svc.register_service<llm::IProviderClient>(&internal_->provider);
    svc.register_service<llm::ProviderClient>(&internal_->provider);
    svc.register_service<capabilities::tool::ToolRegistry>(&internal_->tools.registry_);
    svc.register_service<mcp::MCPManager>(internal_->tools.mcp_.get());

    // Facade 接口（支持测试 Mock）
    svc.register_service<IToolContext>(&internal_->tools);
    svc.register_service<IMemoryContext>(&internal_->memory);
    svc.register_service<IOrchestrationContext>(&internal_->orch);

    // SkillLoader
    svc.register_service<skill::SkillLoader>(&internal_->skill_loader);

    // 核心服务接口 — 若已预注入则跳过，否则创建默认实现
    if (!svc.resolve<core::IFileService>()) {
        internal_->file_svc = core::make_default_file_service();
        svc.register_service<core::IFileService>(internal_->file_svc.get());
    }
    if (!svc.resolve<core::ICommandExecutor>()) {
        internal_->cmd_svc = core::make_default_command_executor();
        svc.register_service<core::ICommandExecutor>(internal_->cmd_svc.get());
    }

    svc.register_service<orchestration::PlanManager>(&internal_->orch.plans_);

    // ─── 事件总线 ──────────────────────────────────────────────
    svc.register_service<base::EventBus>(&event_bus_);

}

void ServiceBootstrap::shutdown() {
    if (!lifecycle_.is_ready()) return;
    lifecycle_.begin_shutdown();

    // 逆序关闭：先关高层服务，再关底层基础设施
    internal_->sub_agent.reset();
    internal_->orch.plugin_loader_.reset();
    internal_->tools.mcp_.reset();
    internal_->memory.history_db_.reset();

    if (internal_->infra.io_context) internal_->infra.io_context->drain();
    if (internal_->infra.util_context) internal_->infra.util_context->drain();
    if (internal_->infra.core_pool) internal_->infra.core_pool->shutdown();

    services_.clear();
    lifecycle_.end_shutdown();
}

std::unique_ptr<workspace::Session> ServiceBootstrap::make_session(std::string session_id) {
    workspace::SessionDeps deps{
        .ws_ctx = ws_ctx_,
        .memory_store = internal_->memory.store_,
        .context_builder = internal_->memory.builder_.get(),
        .thread_pool = internal_->infra.core_pool
    };

    auto session = std::make_unique<workspace::Session>(
        workspace::SessionConfig{
            session_id, settings_.llm.context_length, settings_.context_prune,
            config::SessionType::main, {}
        },
        deps, internal_->tools.registry_);

    // session_id 为空时不自动创建会话，由前端显式创建
    if (!session_id.empty() && internal_->memory.history_db_) {
        session->restore_from_db(*internal_->memory.history_db_);
    }

    return session;
}

} // namespace ben_gear::agent::runtime
