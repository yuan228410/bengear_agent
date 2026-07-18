#include "agent/runtime/runtime_factory.hpp"
#include "agent/runtime/runtime.hpp"

#include "agent/runtime/application/workspace_resolver.hpp"
#include "agent/runtime/application/command_governance.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>

#include "base/log/logger.hpp"
#include "base/platform/platform.hpp"
#include "domain/result.hpp"

#include "capabilities/mcp/mcp_client.hpp"
#include "acp/core/message.hpp"
#include "capabilities/skill/skill_tools.hpp"
#include "memory/memory_tools.hpp"
#include "workspace/workspace_tools.hpp"
#include "workspace/history_tools.hpp"
#include "workflow/workflow_tools.hpp"
#include "capabilities/tool/builtin_tools.hpp"
#include "capabilities/tool/sub_agent_tools.hpp"

namespace ben_gear::agent::runtime {

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

void RuntimeFactory::init_infrastructure(Runtime& runtime) {
    init_http_workflow(runtime);
    init_workspace(runtime);
}

void RuntimeFactory::init_memory_system(Runtime& runtime) {
    init_memory(runtime);
    init_history(runtime);
}

void RuntimeFactory::init_tool_system(Runtime& runtime) {
    init_tools(runtime);
    init_skills(runtime);
    init_mcp(runtime);
}

void RuntimeFactory::init_orchestration(Runtime& runtime) {
    init_workflow(runtime);
    init_sub_agent(runtime);
    init_plugins(runtime);
}

void RuntimeFactory::inject_agent_defaults(Runtime& runtime) {
    auto& agent = runtime.agent();
    if (!agent.file()) agent.set_file(std::make_shared<core::SandboxedFileService>(core::make_default_file_service()));
    if (!agent.web()) agent.set_web(core::make_default_web_service());
    if (!agent.skill()) agent.set_skill(core::make_default_skill_service());
    if (!agent.cmd()) agent.set_cmd(std::make_shared<core::SandboxedCommandExecutor>(core::make_default_command_executor()));
    if (!agent.mcp()) agent.set_mcp(core::make_default_mcp_service());
}

void RuntimeFactory::init_http_workflow(Runtime& runtime) {
    auto& tools = runtime.tool_context_mut();
    auto& orch = runtime.orchestration_context_mut();
    auto& infra = runtime.infrastructure();

    tools.mcp()->set_io_context(infra.util_context.get());
    tools::register_http_tools(tools.registry_mut(), *infra.util_context);
    orch.workflow()->bind_resources(runtime.make_workflow_resources());
}

void RuntimeFactory::init_workspace(Runtime& runtime) {
    auto& memory = runtime.memory_context_mut();
    auto& ws_ctx = runtime.workspace_context();
    std::filesystem::create_directories(ws_ctx.tier_paths.user_dir);
    memory.ws_manager_ = std::make_shared<workspace::WorkspaceManager>(
        ws_ctx.tier_paths.user_dir);
}

void RuntimeFactory::init_memory(Runtime& runtime) {
    auto& memory = runtime.memory_context_mut();
    auto& ws_ctx = runtime.workspace_context();
    auto& settings = runtime.settings();
    auto& skill_loader = runtime.skill_loader_mut();

    memory.store_ = std::make_shared<memory::MemoryStore>(ws_ctx.tier_paths);
    ensure_default_memory_files(runtime);
    memory.builder_ = std::make_unique<memory::ContextBuilder>(
        *memory.store_, skill_loader.get_skills_metadata());
    auto project_dir = ws_ctx.project_path.empty()
        ? settings.workspace
        : std::filesystem::path(std::string(
            ws_ctx.project_path.data(), ws_ctx.project_path.size()));
    memory.builder_->set_project_dir(project_dir);
    if (!settings.agent.system_prompt.empty()) {
        memory.builder_->set_core_prompt(
            std::string(settings.agent.system_prompt.data(),
                        settings.agent.system_prompt.size()));
    }
    memory.builder_->set_inject_project_doc(settings.agent.inject_project_doc);
}

void RuntimeFactory::ensure_default_memory_files(Runtime& runtime) {
    auto& memory = runtime.memory_context_mut();
    auto& ws_ctx = runtime.workspace_context();

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
        memory.store_->write_soul(
            std::string(soul_content, std::strlen(soul_content)),
            base::Tier::global);
    }
}

void RuntimeFactory::init_history(Runtime& runtime) {
    auto& memory = runtime.memory_context_mut();
    auto& ws_ctx = runtime.workspace_context();
    if (memory.history_db_) return;
    auto db_dir = ws_ctx.tier_paths.global_dir;
    std::filesystem::create_directories(db_dir);
    memory.history_db_ = std::make_shared<workspace::HistoryDB>(db_dir / "history.db");
}

void RuntimeFactory::init_tools(Runtime& runtime) {
    auto& tools = runtime.tool_context_mut();
    auto& memory = runtime.memory_context_mut();
    auto& orch = runtime.orchestration_context_mut();
    auto& settings = runtime.settings();
    auto& ws_ctx = runtime.workspace_context();
    auto& infra = runtime.infrastructure();
    auto& skill_loader = runtime.skill_loader_mut();

    application::RequestContext request;
    request.username = ws_ctx.username;
    request.workspace_name = ws_ctx.workspace_name;
    request.session_id = ws_ctx.session_id;

    skill::register_all_tools(tools.registry_mut(), settings.agent.command_timeout,
                              &skill_loader, *infra.util_context);
    memory::register_memory_tools(tools.registry_mut(), memory.store_);
    workspace::register_workspace_tools(tools.registry_mut(), memory.ws_manager_);
    workspace::register_history_tools(tools.registry_mut(), *memory.history_db_, ws_ctx);
    workflow::register_workflow_tools_with_resources(tools.registry_mut(), orch.workflow_, orch.templates_);
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

void RuntimeFactory::init_skills(Runtime& runtime) {
    auto& skill_loader = runtime.skill_loader_mut();
    skill_loader.discover();
    for (auto& def : skill::builtin_skill_definitions()) {
        skill_loader.add_skill(def);
    }
}

void RuntimeFactory::init_mcp(Runtime& runtime) {
    auto& tools = runtime.tool_context_mut();
    auto& settings = runtime.settings();

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

void RuntimeFactory::init_workflow(Runtime& runtime) {
    auto& orch = runtime.orchestration_context_mut();
    orch.templates_->register_template(workflow::templates::code_review());
    orch.templates_->register_template(workflow::templates::documentation());
    orch.templates_->register_template(workflow::templates::refactoring());
    orch.templates_->register_template(workflow::templates::test_generation());
}

void RuntimeFactory::init_sub_agent(Runtime& runtime) {
    auto& tools = runtime.tool_context_mut();
    auto& memory = runtime.memory_context_mut();
    auto& settings = runtime.settings();
    auto& provider = runtime.provider();

    auto sub_agent = std::make_shared<SubAgentRuntime>(
        settings, provider, tools.registry());
    sub_agent->set_context_builder(memory.builder_.get());

    tools::register_sub_agent_tools(tools.registry_mut(), sub_agent);
    auto max_parallel = sub_agent->default_config().max_parallel;
    runtime.set_sub_agent_runtime(std::move(sub_agent));
    log::info_fmt("init: sub_agent (max_parallel={})", max_parallel);
}

void RuntimeFactory::init_plugins(Runtime& runtime) {
    auto& orch = runtime.orchestration_context_mut();
    auto& settings = runtime.settings();

    auto dir = settings.plugins_dir;
    if (dir.empty()) {
        dir = std::filesystem::path(base::platform::os::data_directory()) / "plugins";
    }
    if (std::filesystem::exists(dir)) {
        orch.plugin_loader_ = std::make_unique<plugins::PluginLoader>(dir);
        auto [loaded, errors] = orch.plugin_loader_->load_all();
        if (loaded > 0) {
            log::info_fmt("plugins: loaded {} plugin(s)", loaded);
        }
        for (const auto& err : errors) {
            log::error_fmt("plugins: failed to load: {}", err);
        }

        for (const auto& plugin : orch.plugin_loader_->loaded_plugins()) {
            for (const auto& tool : plugin.tools) {
                register_plugin_tool(runtime, tool);
            }
        }
    }
}

void RuntimeFactory::register_plugin_tool(Runtime& runtime, const plugins::BenGearTool& tool) {
    auto& tools = runtime.tool_context_mut();
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

void RuntimeFactory::init_capabilities(Runtime& runtime) {
    auto& ws_ctx = runtime.workspace_context();
    auto instances = capabilities::CapabilityRegistry::instance().create_all(ws_ctx);
    for (auto& cap : instances) {
        log::info_fmt("capability: initializing '{}'", cap->name());
        cap->init();
    }
    runtime.set_capabilities(std::move(instances));
}

} // namespace ben_gear::agent::runtime
