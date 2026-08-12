#include "agent/runtime/runtime_factory.hpp"
#include "agent/runtime/runtime.hpp"
#include "orchestration/todo_tools.hpp"
#include "compress/compress_engine.hpp"
#include "agent/builtin_agent.hpp"
#include <sstream>

#include "workspace/resolver.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>

#include "log/logger.hpp"
#include "platform/platform.hpp"
#include "domain/result.hpp"

#include "capabilities/mcp/mcp_client.hpp"
#include "acp/core/message.hpp"
#include "capabilities/skill/skill_tools.hpp"
#include "memory/memory_tools.hpp"
#include "workspace/workspace_tools.hpp"
#include "workspace/history_tools.hpp"
#include "capabilities/tool/builtin_tools.hpp"
#include "agent/runtime/sub_agent_tools.hpp"
#include "team/tools.hpp"
#include "team/orchestrator.hpp"
#include "agent/core/events.hpp"
#include "orchestration/serializer.hpp"

#include "agent/core/interfaces.hpp"
#include "agent/runtime/service_bundles.hpp"
#include "agent/runtime/tool_context.hpp"
#include "agent/runtime/memory_context.hpp"
#include "agent/runtime/orchestration_context.hpp"
#include "agent/runtime/sub_agent_runtime.hpp"

namespace ben_gear::agent::runtime {

// ─── 辅助：获取可变引用 ────────────────────────────────────────────
// 通过 ServiceRegistry 获取服务引用（调用方确保已注册）

namespace {

config::Settings& get_settings(Runtime& rt) {
    return rt.services().template resolve_ref<config::Settings>();
}

llm::ProviderClient& get_provider(Runtime& rt) {
    return rt.services().template resolve_ref<llm::ProviderClient>();
}

IToolContext& get_tool_context(Runtime& rt) {
    return rt.services().template resolve_ref<IToolContext>();
}

IOrchestrationContext& get_orch_context(Runtime& rt) {
    return rt.services().template resolve_ref<IOrchestrationContext>();
}

InfrastructureServices& get_infra(Runtime& rt) {
    return rt.services().template resolve_ref<InfrastructureServices>();
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════
//  RuntimeFactory 实现
// ═══════════════════════════════════════════════════════════════════

std::shared_ptr<Runtime> RuntimeFactory::create(
    config::Settings settings,
    workspace::WorkspaceContext ws_ctx) {
    auto runtime = std::shared_ptr<Runtime>(Runtime::make(std::move(settings), std::move(ws_ctx)));
    initialize(*runtime);
    return runtime;
}

std::shared_ptr<Runtime> RuntimeFactory::create_uninitialized(
    config::Settings settings,
    workspace::WorkspaceContext ws_ctx) {
    auto rt = std::shared_ptr<Runtime>(Runtime::make(std::move(settings), std::move(ws_ctx)));
    rt->init();
    return rt;
}

void RuntimeFactory::initialize(Runtime& runtime) {
    runtime.init();  // 创建 InternalServices + 注册默认服务
    runtime.lifecycle().begin_initialization();
    init_infrastructure(runtime);
    init_memory_system(runtime);
    init_tool_system(runtime);
    init_orchestration(runtime);
    runtime.lifecycle().end_initialization();
}

void RuntimeFactory::init_infrastructure(Runtime& rt) {
    init_http(rt);
    init_workspace(rt);
}

void RuntimeFactory::init_memory_system(Runtime& rt) {
    init_memory(rt);
    init_history(rt);
}

void RuntimeFactory::init_tool_system(Runtime& rt) {
    init_tools(rt);
    init_skills(rt);
    init_mcp(rt);
}

void RuntimeFactory::init_orchestration(Runtime& rt) {
    init_builtin_agents(rt);
    init_sub_agent(rt);
    init_team(rt);
    init_plugins(rt);
}

// ─── 基础设施初始化 ────────────────────────────────────────────────

void RuntimeFactory::init_http(Runtime& rt) {
    auto& tools = get_tool_context(rt);
    auto& infra = get_infra(rt);

    tools.mcp()->set_io_context(infra.util_context.get());
    tools::register_http_tools(tools.registry_mut(), *infra.util_context,
                               rt.services().template resolve_ref<net::TlsEngine>());
}

void RuntimeFactory::init_workspace(Runtime& rt) {
    auto ws_ctx = rt.services().resolve<workspace::WorkspaceContext>();
    if (!ws_ctx) return;

    std::filesystem::create_directories(ws_ctx->tier_paths.user_dir);
    auto ws_manager = std::make_shared<workspace::WorkspaceManager>(
        ws_ctx->tier_paths.user_dir);
    rt.services().register_shared(ws_manager);
}

// ─── 记忆系统初始化 ────────────────────────────────────────────────

void RuntimeFactory::init_memory(Runtime& rt) {
    auto ws_ctx = rt.services().resolve<workspace::WorkspaceContext>();
    auto& settings = get_settings(rt);
    if (!ws_ctx) return;

    auto store = std::make_shared<memory::MemoryStore>(ws_ctx->tier_paths);
    ensure_default_memory_files(rt, *store, *ws_ctx);

    auto skill_loader = rt.services().resolve<skill::SkillLoader>();
    auto builder = std::make_unique<memory::ContextBuilder>(
        *store, skill_loader ? skill_loader->get_skills_metadata() : std::string{});

    auto project_dir = ws_ctx->project_path.empty()
        ? settings.workspace
        : std::filesystem::path(std::string(
            ws_ctx->project_path.data(), ws_ctx->project_path.size()));
    builder->set_project_dir(project_dir);

    if (!settings.agent.system_prompt.empty()) {
        builder->set_core_prompt(
            std::string(settings.agent.system_prompt.data(),
                        settings.agent.system_prompt.size()));
    }
    builder->set_inject_project_doc(settings.agent.inject_project_doc);

    rt.services().register_shared(store);
    rt.services().register_service<memory::ContextBuilder>(builder.get());

    // 注入到 MemoryContext（通过友元直接访问）
    auto& mem_ctx = rt.bootstrap().memory();
    mem_ctx.store_ = store;
    mem_ctx.builder_ = std::move(builder);
}

void RuntimeFactory::ensure_default_memory_files(Runtime&,
    memory::MemoryStore& store, const workspace::WorkspaceContext& ws_ctx) {
    auto memory_dir = ws_ctx.tier_paths.dir(base::Tier::global) / "memory";
    std::filesystem::create_directories(memory_dir);
    auto soul_path = memory_dir / "SOUL.md";
    if (!std::filesystem::exists(soul_path)) {
        const char* soul_content =
            "# Soul\n"
            "\n"
            "- Be helpful, precise, and proactive.\n"
            "- Anticipate user needs and suggest improvements.\n"
            "- Admit uncertainty rather than fabricate.\n"
            "- Learn from user feedback and past interactions.\n";
        store.write_soul(
            std::string(soul_content, std::strlen(soul_content)),
            base::Tier::global);
    }
}

void RuntimeFactory::init_history(Runtime& rt) {
    auto ws_ctx = rt.services().resolve<workspace::WorkspaceContext>();
    if (!ws_ctx) return;

    auto db_dir = ws_ctx->tier_paths.global_dir;
    std::filesystem::create_directories(db_dir);
    auto history_db = std::make_shared<workspace::HistoryDB>(db_dir / "history.db");
    rt.services().register_service<workspace::HistoryDB>(history_db.get());

    // 注入到 MemoryContext（通过友元直接访问）
    rt.bootstrap().memory().history_db_ = std::move(history_db);
}

// ─── 工具系统初始化 ────────────────────────────────────────────────

void RuntimeFactory::init_tools(Runtime& rt) {
    auto& tools = get_tool_context(rt);
    auto ws_ctx = rt.services().resolve<workspace::WorkspaceContext>();
    auto& settings = get_settings(rt);
    auto& infra = get_infra(rt);

    // 获取动态注入的服务
    auto* history_db = rt.services().resolve<workspace::HistoryDB>();
    auto memory_store_sp = rt.services().resolve_shared<memory::MemoryStore>();
    auto ws_manager_sp = rt.services().resolve_shared<workspace::WorkspaceManager>();
    auto& skill_loader = rt.services().resolve_ref<skill::SkillLoader>();

    // 解析请求上下文
    ::ben_gear::base::core::RequestContext request;
    request.username = ws_ctx ? ws_ctx->username : std::string();
    request.workspace_name = ws_ctx ? ws_ctx->workspace_name : std::string();
    request.session_id = ws_ctx ? ws_ctx->session_id : std::string();

    ben_gear::tools::register_builtin_tools(tools.registry_mut(), settings.agent.command_timeout);
    skill::register_all_tools(tools.registry_mut(), settings.agent.command_timeout,
                              &skill_loader, *infra.util_context,
                              rt.services().template resolve_ref<net::TlsEngine>(),
                              rt.services().template resolve_ref<compress::CompressEngine>());
    if (memory_store_sp) {
        memory::register_memory_tools(tools.registry_mut(), memory_store_sp);
    }
    if (ws_manager_sp) {
        workspace::register_workspace_tools(tools.registry_mut(), ws_manager_sp);
    }
    if (history_db && ws_ctx) {
        workspace::register_history_tools(tools.registry_mut(), *history_db, *ws_ctx);
    }
    // update_todo 工具 — 通用 TODO 管理
    orchestration::register_todo_tools(tools.registry_mut(), rt.services());
}

void RuntimeFactory::init_skills(Runtime& rt) {
    auto* skill_loader = rt.services().resolve<skill::SkillLoader>();
    if (!skill_loader) return;

    skill_loader->discover();
}

void RuntimeFactory::init_mcp(Runtime& rt) {
    auto& tools = get_tool_context(rt);
    auto& settings = get_settings(rt);

    if (!settings.mcp_servers.empty()) {
        tools.mcp()->load_servers(settings.mcp_servers);
        for (const auto& tool_def : tools.mcp()->all_tool_definitions()) {
            std::string raw_name(tool_def.name);
            std::string mcp_name = "mcp_" + raw_name;
            auto mgr = tools.mcp();
            tools.registry_mut().register_tool(
                mcp_name,
                tool_def.description,
                tool_def.parameters,
                [mgr, raw_name](const Json& args) -> std::string {
                    return mgr->execute_tool(raw_name, args);
                });
        }
    }
}

// ─── 编排系统初始化 ────────────────────────────────────────────────

void RuntimeFactory::init_sub_agent(Runtime& rt) {
    auto& tools = get_tool_context(rt);
    auto& provider = get_provider(rt);
    auto& settings = get_settings(rt);

    auto sub_agent = std::make_shared<SubAgentRuntime>(
        settings, provider, tools.registry());

    tools::register_sub_agent_tools(tools.registry_mut(), sub_agent);

    // 注册自定义子 Agent（从目录加载 .md 文件）
    auto& cfg = settings.agent.sub_agent;
    std::string agents_dir = cfg.sub_agents_dir;
    if (agents_dir.empty()) {
        agents_dir = (std::filesystem::path(base::platform::os::data_directory()) / "agents/sub").string();
    }
    tools::register_custom_sub_agents(tools.registry_mut(), sub_agent, agents_dir);
    // 工作空间目录（用于创建 agent 时支持三层层级）
    std::string workspace_dir;
    if (auto* ws_ctx = rt.services().resolve<workspace::WorkspaceContext>()) {
        workspace_dir = ws_ctx->tier_paths.workspace_dir.string();
    }
    tools::register_sub_agent_management_tools(tools.registry_mut(), sub_agent, agents_dir, workspace_dir);

    // 注入 EventBus，使主 Agent 委派的子 Agent 也能推送进度事件
    if (auto* eb = rt.services().resolve<base::EventBus>()) {
        sub_agent->set_event_bus(eb);
    }

    auto max_parallel = sub_agent->default_config().max_parallel;
    // 使用 register_shared 转移所有权到 ServiceRegistry，避免裸指针悬空
    rt.services().register_shared<SubAgentRuntime>(sub_agent);
    log::info_fmt("init: sub_agent (max_parallel={})", max_parallel);
}

void RuntimeFactory::init_builtin_agents(Runtime& rt) {
    auto agent_reg = std::make_shared<agent::BuiltinAgentRegistry>(
        agent::BuiltinAgentRegistry::load_from_directory(
            (std::filesystem::path(base::platform::os::data_directory())
             / "agents/primary").string()));

    // fallback：目录为空时注册硬编码默认值，保证启动不挂
    if (agent_reg->agents().empty()) {
        agent_reg->register_agent({
            .name = "build",
            .description = "构建模式：直接执行，无计划审批（默认）",
            .category = agent::AgentCategory::primary,
            .mode = agent::ExecutionMode::react,
            .system_prompt =
                "## Build Mode\n"
                "Execute tasks directly and efficiently.\n"
                "- Act immediately, no approval needed.\n"
                "- Report concisely what you did and why.\n",
        });
        agent_reg->register_agent({
            .name = "plan",
            .description = "计划模式：先规划方案，确认后逐步执行",
            .category = agent::AgentCategory::primary,
            .mode = agent::ExecutionMode::plan,
            .system_prompt =
                "## Plan Mode\n"
                "Think before you act.\n"
                "- Explore scope, produce structured plan.\n"
                "- Present for review. Do NOT execute until approved.\n"
                "- Once approved, execute step by step.\n",
        });
        log::info_fmt("init: builtin agents fallback (hardcoded defaults)");
    }

    rt.services().register_shared(agent_reg);
    log::info_fmt("init: builtin agents loaded ({} total)", agent_reg->agents().size());

    // 注册 primary agent 管理工具：agent_create / agent_remove
    auto& tools = get_tool_context(rt);
    std::string workspace_dir;
    if (auto* ws_ctx = rt.services().resolve<workspace::WorkspaceContext>()) {
        workspace_dir = ws_ctx->tier_paths.workspace_dir.string();
    }
    auto data_dir = base::platform::os::data_directory();

    agent::register_primary_agent_tools(tools.registry_mut(), agent_reg, workspace_dir, data_dir);

    log::info_fmt("init: builtin agents loaded ({} total)", agent_reg->agents().size());
}

void RuntimeFactory::init_team(Runtime& rt) {
    auto& settings = get_settings(rt);
    auto& provider = get_provider(rt);
    auto& tools = get_tool_context(rt);

    std::string workspace_dir;
    if (auto* ws_ctx = rt.services().resolve<workspace::WorkspaceContext>()) {
        workspace_dir = ws_ctx->tier_paths.workspace_dir.string();
    }

    auto* history_db = rt.services().resolve<workspace::HistoryDB>();
    auto orchestrator = std::make_shared<team::TeamOrchestrator>(
        settings, provider, tools.registry(),
        rt.services().resolve<base::EventBus>(), history_db);
    rt.services().register_shared(orchestrator);

    // 注册团队工具
    team::register_team_tools(tools.registry_mut(), orchestrator, workspace_dir);

    log::info_fmt("init: team orchestration");
}

void RuntimeFactory::init_plugins(Runtime& rt) {
    auto& orch = get_orch_context(rt);
    auto& settings = get_settings(rt);

    auto dir = settings.plugins_dir;
    if (dir.empty()) {
        dir = std::filesystem::path(base::platform::os::data_directory()) / "plugins";
    }
    if (std::filesystem::exists(dir)) {
        orch.set_plugin_loader(std::make_unique<plugins::PluginLoader>(dir));
        auto [loaded, errors] = orch.plugin_loader_ptr()->load_all();
        if (loaded > 0) {
            log::info_fmt("plugins: loaded {} plugin(s)", loaded);
        }
        for (const auto& err : errors) {
            log::error_fmt("plugins: failed to load: {}", err);
        }

        for (const auto& plugin : orch.plugin_loader_ptr()->loaded_plugins()) {
            for (const auto& tool : plugin.tools) {
                register_plugin_tool(rt, tool);
            }
        }
        rt.services().register_service<plugins::PluginLoader>(orch.plugin_loader_ptr());
    }
}

void RuntimeFactory::register_plugin_tool(Runtime& rt, const plugins::BenGearTool& tool) {
    auto& tools = get_tool_context(rt);
    auto tool_name = std::string(tool.name);

    if (tools.registry().has_tool(std::string_view(tool_name.data(), tool_name.size()))) {
        log::warn_fmt("plugins: skipping duplicate tool '{}', already registered by builtins", tool.name);
        return;
    }

    std::vector<std::pair<std::string, capabilities::tool::ToolParameterSchema>> params;
    auto params_json = Json::parse(tool.params_json ? tool.params_json : "[]");
    if (params_json.is_array()) {
        for (const auto& p : params_json) {
            capabilities::tool::ToolParameterSchema schema;
            schema.type = p.value("type", "string");
            schema.description = p.value("description", "");
            schema.required = p.value("required", false);
            if (p.contains("enum_values") && p["enum_values"].is_array()) {
                for (const auto& v : p["enum_values"]) {
                    schema.enum_values.push_back(v.get<std::string>());
                }
            }
            params.emplace_back(p.value("name", ""), std::move(schema));
        }
    }

    auto* exec_fn = tool.execute;
    tools.registry_mut().register_tool(
        tool_name,
        std::string(tool.description),
        params,
        [exec_fn](const Json& args) -> std::string {
            auto result = exec_fn(args.dump().c_str());
            return std::string(result);
        });

    log::info_fmt("plugins: registered tool '{}'", tool.name);
}

} // namespace ben_gear::agent::runtime
