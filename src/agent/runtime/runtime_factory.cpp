#include "agent/runtime/runtime_factory.hpp"
#include "agent/runtime/runtime.hpp"
#include "compress/compress_engine.hpp"

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
#include "agent/core/events.hpp"
#include "orchestration/serializer.hpp"
#include "capabilities/capability_registry.hpp"

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
    init_capabilities(runtime);
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
    init_sub_agent(rt);
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
    // update_todo 工具 — 通用 TODO 管理，计划模式/非计划模式均可用
    // LLM 通过此工具拆分多步骤任务，创建/更新/删除/清空 TODO 项
    // 实现通过 ServiceRegistry 获取 TodoManager 和 EventBus，运行时按需解析
    auto& svc_ref = rt.services();
    tools.registry_mut().register_tool(
        std::string("update_todo"),
        std::string("STRICT WORKFLOW - follow these steps IN ORDER:\n"
                    "1. FIRST: call this tool ONCE for EACH step to create all TODOs (set status='pending')\n"
                    "2. THEN: execute each step one by one\n"
                    "3. Before each step: update its status to 'running'\n"
                    "4. After each step: update its status to 'succeeded'\n"
                    "Example for 'search tool in 3 steps': create 3 TODOs first, then execute.\n"
                    "Simple questions: skip this tool."),
        {
            {std::string("action"),
             {std::string("string"),
              std::string("Action to perform: create, update, delete, or clear"),
              {std::string("create"), std::string("update"),
               std::string("delete"), std::string("clear")}, true}},
            {std::string("todo_id"),
             {std::string("string"),
              std::string("Todo item ID (auto-generated if empty for 'create')"),
              {}, false}},
            {std::string("title"),
             {std::string("string"),
              std::string("Title/description of the todo item"),
              {}, false}},
            {std::string("status"),
             {std::string("string"),
              std::string("Status: pending, running, succeeded, failed, blocked, skipped"),
              {std::string("pending"), std::string("running"),
               std::string("succeeded"), std::string("failed"),
               std::string("blocked"), std::string("skipped")}, false}},
            {std::string("progress"),
             {std::string("integer"),
              std::string("Progress percentage (0-100)"),
              {}, false}},
            {std::string("summary"),
             {std::string("string"),
              std::string("Result summary or notes about this item"),
              {}, false}},
        },
        [&svc_ref](const Json& args) -> std::string {
            auto* todo_mgr = svc_ref.resolve<orchestration::TodoManager>();
            if (!todo_mgr) {
                return std::string(R"({"error":"todo service not available"})");
            }

            auto action = args.value("action", std::string("create"));

            // clear — 清空所有 TODO
            if (action == "clear") {
                todo_mgr->reset();
                if (auto* eb = svc_ref.resolve<base::EventBus>()) {
                    eb->publish(agent::TodoUpdateEvent{
                        {}, {}, {}, {}, {}, std::string("clear"), 0, {}});
                }
                return orchestration::to_json_string(todo_mgr->state());
            }

            // delete — 删除指定项
            if (action == "delete") {
                auto todo_id = args.value("todo_id", std::string());
                if (todo_id.empty()) {
                    return std::string(R"({"error":"todo_id required for delete"})");
                }
                auto delta = todo_mgr->remove(std::string_view(todo_id.data(), todo_id.size()));
                if (auto* eb = svc_ref.resolve<base::EventBus>()) {
                    eb->publish(agent::TodoUpdateEvent{
                        std::move(todo_id), {}, {}, {}, {}, std::string("deleted"), 0, {}});
                }
                return orchestration::to_json_string(todo_mgr->state());
            }

            // create / update — 新增或更新 TODO 项
            orchestration::TodoItem item;
            item.todo_id = args.value("todo_id", std::string());
            // action 为 create 且未传 todo_id 时自动生成
            if (action == "create" && item.todo_id.empty()) {
                auto count = todo_mgr->state().items.size() + 1;
                item.todo_id = "todo:" + std::to_string(count);
            }
            item.title = args.value("title", std::string());
            item.progress = args.value("progress", 0);
            item.result_summary = args.value("summary", std::string());

            // 约束：create 强制设为 pending，不允许直接创建为 running/succeeded
            if (action == "create") {
                item.status = orchestration::TodoStatus::pending;
            } else {
                // update 必须有 todo_id，否则拒绝
                if (item.todo_id.empty()) {
                    return std::string(R"({"error":"todo_id required for update"})");
                }
                item.status = orchestration::todo_status_from_string(
                    args.value("status", std::string("pending")));
            }

            auto delta = todo_mgr->upsert(std::move(item), action);

            // 发布事件通知前端
            if (auto* eb = svc_ref.resolve<base::EventBus>()) {
                eb->publish(agent::TodoUpdateEvent{
                    delta.item.todo_id,
                    delta.session_id,
                    delta.workspace,
                    delta.item.title,
                    std::string(orchestration::to_string(delta.item.status)),
                    std::move(delta.action),
                    delta.item.progress,
                    delta.item.result_summary});
            }

            return orchestration::to_json_string(todo_mgr->state());
        });
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
        agents_dir = (std::filesystem::path(base::platform::os::data_directory()) / "sub_agents").string();
    }
    tools::register_custom_sub_agents(tools.registry_mut(), sub_agent, agents_dir);

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

} // namespace ben_gear::agent::runtime
