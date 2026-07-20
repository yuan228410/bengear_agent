#include "agent/runtime/runtime_factory.hpp"
#include "agent/runtime/runtime.hpp"

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
#include "workflow/workflow_tools.hpp"
#include "capabilities/tool/builtin_tools.hpp"
#include "agent/runtime/sub_agent_tools.hpp"
#include "capabilities/capability_registry.hpp"

#include "agent/core/agent_core.hpp"
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
    return *rt.services().template resolve<config::Settings>();
}

llm::ProviderClient& get_provider(Runtime& rt) {
    return *rt.services().template resolve<llm::ProviderClient>();
}

IToolContext& get_tool_context(Runtime& rt) {
    return *rt.services().template resolve<IToolContext>();
}

IMemoryContext& get_memory_context(Runtime& rt) {
    return *rt.services().template resolve<IMemoryContext>();
}

IOrchestrationContext& get_orch_context(Runtime& rt) {
    return *rt.services().template resolve<IOrchestrationContext>();
}

InfrastructureServices& get_infra(Runtime& rt) {
    return *rt.services().template resolve<InfrastructureServices>();
}

core::Agent& get_agent(Runtime& rt) {
    return *rt.services().template resolve<core::Agent>();
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════
//  RuntimeFactory 实现
// ═══════════════════════════════════════════════════════════════════

std::shared_ptr<Runtime> RuntimeFactory::create(
    config::Settings settings,
    workspace::WorkspaceContext ws_ctx) {
    auto runtime = std::shared_ptr<Runtime>(new Runtime(std::move(settings), std::move(ws_ctx)));
    initialize(*runtime);
    return runtime;
}

std::shared_ptr<Runtime> RuntimeFactory::create_uninitialized(
    config::Settings settings,
    workspace::WorkspaceContext ws_ctx) {
    return std::shared_ptr<Runtime>(new Runtime(std::move(settings), std::move(ws_ctx)));
}

void RuntimeFactory::initialize(Runtime& runtime) {
    runtime.lifecycle().begin_initialization();
    init_infrastructure(runtime);
    init_memory_system(runtime);
    init_tool_system(runtime);
    init_orchestration(runtime);
    init_capabilities(runtime);
    inject_agent_defaults(runtime);
    runtime.lifecycle().end_initialization();
}

void RuntimeFactory::init_infrastructure(Runtime& rt) {
    init_http_workflow(rt);
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
    init_workflow(rt);
    init_sub_agent(rt);
    init_plugins(rt);
}

void RuntimeFactory::inject_agent_defaults(Runtime& rt) {
    auto& agent = get_agent(rt);
    if (!agent.file()) agent.set_file(std::make_shared<core::SandboxedFileService>(core::make_default_file_service()));
    if (!agent.web()) agent.set_web(core::make_default_web_service());
    if (!agent.skill()) agent.set_skill(core::make_default_skill_service());
    if (!agent.cmd()) agent.set_cmd(std::make_shared<core::SandboxedCommandExecutor>(core::make_default_command_executor()));
    if (!agent.mcp()) agent.set_mcp(core::make_default_mcp_service());
}

// ─── 基础设施初始化 ────────────────────────────────────────────────

void RuntimeFactory::init_http_workflow(Runtime& rt) {
    auto& tools = get_tool_context(rt);
    auto& orch = get_orch_context(rt);
    auto& infra = get_infra(rt);

    tools.mcp()->set_io_context(infra.util_context.get());
    tools::register_http_tools(tools.registry_mut(), *infra.util_context);
    orch.workflow()->bind_resources(make_workflow_resources_for(rt));
}

void RuntimeFactory::init_workspace(Runtime& rt) {
    auto ws_ctx = rt.services().resolve<workspace::WorkspaceContext>();
    if (!ws_ctx) return;

    std::filesystem::create_directories(ws_ctx->tier_paths.user_dir);
    auto ws_manager = std::make_shared<workspace::WorkspaceManager>(
        ws_ctx->tier_paths.user_dir);
    rt.services().register_service<workspace::WorkspaceManager>(ws_manager.get());
    // 保持 shared_ptr 存活，防止裸指针悬空（参照 MemoryStore 的两阶段注册模式）
    rt.services().register_service<std::shared_ptr<workspace::WorkspaceManager>>(
        std::make_unique<std::shared_ptr<workspace::WorkspaceManager>>(ws_manager));
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

    rt.services().register_service<memory::MemoryStore>(store.get());
    rt.services().register_service<memory::ContextBuilder>(builder.get());

    // 注入到 IMemoryContext 实现中（由 Runtime 的 InternalServices 持有）
    auto* mem_ctx = dynamic_cast<MemoryContext*>(&get_memory_context(rt));
    if (mem_ctx) {
        mem_ctx->store_ = store;
        mem_ctx->builder_ = std::move(builder);
    }

    // 重新注册为 shared_ptr 供外部使用
    rt.services().register_service<std::shared_ptr<memory::MemoryStore>>(
        std::make_unique<std::shared_ptr<memory::MemoryStore>>(store));
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

    // 注入到 IMemoryContext 实现
    auto* mem_ctx = dynamic_cast<MemoryContext*>(&get_memory_context(rt));
    if (mem_ctx) {
        mem_ctx->history_db_ = std::move(history_db);
    }
}

// ─── 工具系统初始化 ────────────────────────────────────────────────

void RuntimeFactory::init_tools(Runtime& rt) {
    auto& tools = get_tool_context(rt);
    auto ws_ctx = rt.services().resolve<workspace::WorkspaceContext>();
    auto& settings = get_settings(rt);
    auto& infra = get_infra(rt);

    // 获取动态注入的服务
    auto* history_db = rt.services().resolve<workspace::HistoryDB>();
    auto* memory_store_sp = rt.services().resolve<std::shared_ptr<memory::MemoryStore>>();
    auto* ws_manager_sp = rt.services().resolve<std::shared_ptr<workspace::WorkspaceManager>>();
    auto* orch_ctx = rt.services().resolve<IOrchestrationContext>();
    auto& skill_loader = *rt.services().resolve<skill::SkillLoader>();

    // 解析请求上下文
    ::ben_gear::core::RequestContext request;
    request.username = ws_ctx ? ws_ctx->username : std::string();
    request.workspace_name = ws_ctx ? ws_ctx->workspace_name : std::string();
    request.session_id = ws_ctx ? ws_ctx->session_id : std::string();

    ben_gear::tools::register_builtin_tools(tools.registry_mut(), settings.agent.command_timeout);
    skill::register_all_tools(tools.registry_mut(), settings.agent.command_timeout,
                              &skill_loader, *infra.util_context);
    if (memory_store_sp) {
        memory::register_memory_tools(tools.registry_mut(), *memory_store_sp);
    }
    if (ws_manager_sp) {
        workspace::register_workspace_tools(tools.registry_mut(), *ws_manager_sp);
    }
    if (history_db && ws_ctx) {
        workspace::register_history_tools(tools.registry_mut(), *history_db, *ws_ctx);
    }
    if (orch_ctx) {
        workflow::register_workflow_tools_with_resources(
            tools.registry_mut(), orch_ctx->workflow(), orch_ctx->templates());
    }

    // TODO 工具（内部实现，由 agent session 处理）
    tools.registry_mut().register_tool(
        std::string("update_todo"),
        std::string("Update the session TODO list"),
        {
            {std::string("action"),
             {std::string("string"),
              std::string("set_items, update_item, or clear"),
              {std::string("set_items"), std::string("update_item"),
               std::string("clear")}, true}},
            {std::string("items"),
             {std::string("array"),
              std::string("TODO items for set_items"), {}, false}},
            {std::string("item"),
             {std::string("object"),
              std::string("Single TODO item"), {}, false}},
        },
        [](const Json&) -> std::string {
            return std::string("handled by agent session");
        });
}

void RuntimeFactory::init_skills(Runtime& rt) {
    auto* skill_loader = rt.services().resolve<skill::SkillLoader>();
    if (!skill_loader) return;

    skill_loader->discover();
    for (auto& def : skill::builtin_skill_definitions()) {
        skill_loader->add_skill(def);
    }
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

void RuntimeFactory::init_workflow(Runtime& rt) {
    auto& orch = get_orch_context(rt);
    orch.templates()->register_template(workflow::templates::code_review());
    orch.templates()->register_template(workflow::templates::documentation());
    orch.templates()->register_template(workflow::templates::refactoring());
    orch.templates()->register_template(workflow::templates::test_generation());
}

void RuntimeFactory::init_sub_agent(Runtime& rt) {
    auto& tools = get_tool_context(rt);
    auto& provider = get_provider(rt);
    auto& settings = get_settings(rt);

    auto* memory_ctx = rt.services().resolve<IMemoryContext>();

    auto sub_agent = std::make_shared<SubAgentRuntime>(
        settings, provider, tools.registry());

    // 通过友元关系设置 context_builder
    auto* mem_impl = dynamic_cast<MemoryContext*>(memory_ctx);
    if (mem_impl && mem_impl->builder_) {
        sub_agent->set_context_builder(mem_impl->builder_.get());
    }

    tools::register_sub_agent_tools(tools.registry_mut(), sub_agent);
    auto max_parallel = sub_agent->default_config().max_parallel;
    rt.services().register_service<SubAgentRuntime>(sub_agent.get());
    log::info_fmt("init: sub_agent (max_parallel={})", max_parallel);
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

void RuntimeFactory::init_capabilities(Runtime& rt) {
    auto ws_ctx = rt.services().resolve<workspace::WorkspaceContext>();
    if (!ws_ctx) return;

    auto instances = capabilities::CapabilityRegistry::instance().create_all(*ws_ctx);
    for (auto& cap : instances) {
        log::info_fmt("capability: initializing '{}'", cap->name());
        cap->init();
    }
    // Capabilities owned by InternalServices
}

// ═══════════════════════════════════════════════════════════════════
//  辅助：创建 WorkflowResources
// ═══════════════════════════════════════════════════════════════════

workflow::WorkflowResources RuntimeFactory::make_workflow_resources_for(Runtime& rt) {
    auto self = rt.shared_from_this();
    std::weak_ptr<Runtime> weak_self = self;

    workflow::WorkflowResources res;
    res.tools = rt.services().resolve<capabilities::tool::ToolRegistry>();
    res.settings = rt.services().resolve<config::Settings>();
    auto* infra = rt.services().resolve<InfrastructureServices>();
    res.wf_context = infra ? infra->wf_context.get() : nullptr;
    res.lifetime_context = {};

    auto* provider = rt.services().resolve<llm::ProviderClient>();
    auto* tool_reg = rt.services().resolve<capabilities::tool::ToolRegistry>();
    auto* settings = rt.services().resolve<config::Settings>();

    res.run_chat_async = [weak_self, provider, tool_reg, settings](
        net::EventLoop& loop,
        const std::string& session_id,
        std::string prompt,
        std::string model_override) -> net::Task<llm::ChatResult> {
        (void)session_id;
        auto locked = weak_self.lock();
        if (!locked || !provider || !tool_reg || !settings) {
            co_return llm::ChatResult::internal_error(
                std::string("workflow resources expired"));
        }
        llm::ConversationHistory history;
        auto& sp = settings->agent.system_prompt;
        history.set_system_prompt(sp.empty()
            ? std::string("You are a helpful assistant.")
            : std::string(sp.data(), sp.size()));
        history.add_user(std::string_view(prompt.data(), prompt.size()));
        std::string model(model_override.data(), model_override.size());
        auto result = co_await provider->chat_with_tools_async(
            loop, history, *tool_reg, {}, net::CancellationToken{},
            model.empty() ? std::string() : model_override);
        co_return llm::ChatResult::ok(
            std::string(result.dump()),
            std::string(result.dump()));
    };
    return res;
}

} // namespace ben_gear::agent::runtime
