#include "agent/runtime/runtime.hpp"

#include "agent/runtime/application/workspace_resolver.hpp"
#include "agent/runtime/application/command_governance.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>

#include "base/log/logger.hpp"
#include "domain/result.hpp"

#include "capabilities/mcp/mcp_client.hpp"

#include "acp/core/message.hpp"
#include "capabilities/tool/skill_tools.hpp"
#include "capabilities/tool/memory_tools.hpp"
#include "capabilities/tool/workspace_tools.hpp"
#include "capabilities/tool/history_tools.hpp"
#include "capabilities/tool/workflow_tools.hpp"
#include "capabilities/tool/builtin_tools.hpp"

namespace ben_gear::agent::runtime {

Runtime::Runtime(config::Settings settings, workspace::WorkspaceContext ws_ctx)
    : settings_(std::move(settings)),
      provider_(settings_),
      ws_ctx_(std::move(ws_ctx)),
      infra_{
          std::make_shared<base::concurrency::ThreadPool>(
              base::concurrency::to_thread_pool_config(settings_.thread_pool)),
          std::make_shared<net::IoContext>("io"),
          std::make_shared<net::IoContext>("workflow"),
          std::make_shared<net::IoContext>("util"),
      },
      tools_(settings_.mcp.read_buffer_size),
      orch_{
          std::make_shared<workflow::WorkflowEngine>(
              workflow::WorkflowResources{}, nullptr),
          std::make_shared<workflow::WorkflowTemplateLibrary>(),
      },
      skill_loader_(skill::make_skill_loader(ws_ctx_.tier_paths)),
      max_tool_steps_(settings_.agent.max_tool_steps),
      max_tool_calls_(settings_.agent.max_tool_calls),
      max_tool_calls_per_step_(settings_.agent.max_tool_calls_per_step) {
}

Runtime::~Runtime() = default;

workspace::HistoryDB& Runtime::history_db() noexcept { return *memory_.history_db_; }

void Runtime::post_init() {
    init_all();
    post_initialized_ = true;
}

void Runtime::init_all() {
    init_infrastructure();
    init_memory_system();
    init_tool_system();
    init_orchestration();
    init_capabilities();
    inject_agent_defaults();
}

void Runtime::init_infrastructure() {
    init_http_workflow();
    init_workspace();
}

void Runtime::init_memory_system() {
    init_memory();
    init_history();
}

void Runtime::init_tool_system() {
    init_tools();
    init_skills();
    init_mcp();
}

void Runtime::init_orchestration() {
    init_workflow();
    init_sub_agent();
    init_plugins();
}

void Runtime::inject_agent_defaults() {
    if (!agent_.file()) agent_.set_file(std::make_shared<core::SandboxedFileService>(core::make_default_file_service()));
    if (!agent_.web()) agent_.set_web(core::make_default_web_service());
    if (!agent_.skill()) agent_.set_skill(core::make_default_skill_service());
    if (!agent_.cmd()) agent_.set_cmd(std::make_shared<core::SandboxedCommandExecutor>(core::make_default_command_executor()));
    if (!agent_.mcp()) agent_.set_mcp(core::make_default_mcp_service());
}

void Runtime::init_http_workflow() {
    tools_.mcp_->set_io_context(infra_.util_context.get());
    tools::register_http_tools(tools_.registry_, *infra_.util_context);
    orch_.workflow_->bind_resources(make_workflow_resources());
    tools::register_workflow_tools_with_resources(tools_.registry_, orch_.workflow_, orch_.templates_);
}

void Runtime::init_workspace() {
    memory_.ws_manager_ = std::make_shared<workspace::WorkspaceManager>(
        ws_ctx_.tier_paths.user_dir);
}

void Runtime::init_memory() {
    memory_.store_ = std::make_shared<memory::MemoryStore>(ws_ctx_.tier_paths);
    ensure_default_memory_files();
    memory_.builder_ = std::make_unique<memory::ContextBuilder>(
        *memory_.store_, skill_loader_.get_skills_metadata());
    auto project_dir = ws_ctx_.project_path.empty()
        ? settings_.workspace
        : std::filesystem::path(std::string(
            ws_ctx_.project_path.data(), ws_ctx_.project_path.size()));
    memory_.builder_->set_project_dir(project_dir);
    if (!settings_.agent.system_prompt.empty()) {
        memory_.builder_->set_core_prompt(
            std::string(settings_.agent.system_prompt.data(),
                        settings_.agent.system_prompt.size()));
    }
    memory_.builder_->set_inject_project_doc(settings_.agent.inject_project_doc);
}

void Runtime::ensure_default_memory_files() {
    auto soul_path = ws_ctx_.tier_paths.dir(base::Tier::global) / "memory" / "SOUL.md";
    if (!std::filesystem::exists(soul_path)) {
        const char* soul_content =
            "# Soul\n"
            "\n"
            "You are BenGear, an AI agent for assisting users with tasks.\n"
            "\n"
            "## Core Capabilities\n"
            "- Understand and respond to user needs.\n"
            "- Use tools to inspect information, answer questions, and complete tasks.\n"
            "- Preserve project instructions, workspace context, and user-approved constraints.\n";
        memory_.store_->write_soul(
            std::string(soul_content, std::strlen(soul_content)),
            base::Tier::global);
    }
    auto user_dir = ws_ctx_.tier_paths.dir(base::Tier::user) / "memory";
    std::filesystem::create_directories(user_dir);
    auto user_path = user_dir / "USER.md";
    if (!std::filesystem::exists(user_path)) {
        auto username = std::string(ws_ctx_.username.data(), ws_ctx_.username.size());
        std::string user_content = "# User\n\nUsername: " + username + "\n";
        std::ofstream file(user_path, std::ios::binary);
        if (file) {
            file.write(user_content.data(), static_cast<std::streamsize>(user_content.size()));
        }
    }
}

void Runtime::init_history() {
    auto db_path = ws_ctx_.tier_paths.user_dir / "history.db";
    memory_.history_db_ = std::make_unique<workspace::HistoryDB>(db_path);
}

void Runtime::init_tools() {
    tools::register_all_tools(tools_.registry_, settings_.agent.command_timeout,
                              &skill_loader_, *infra_.util_context);
    auto request = request_context();
    tools::register_memory_tools(tools_.registry_, memory_.store_);
    tools::register_workspace_tools(tools_.registry_, memory_.ws_manager_);
    tools::register_history_tools(tools_.registry_, *memory_.history_db_, ws_ctx_);
    tools::register_workflow_tools_with_resources(tools_.registry_, orch_.workflow_, orch_.templates_);
    tools_.registry_.register_tool(
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

void Runtime::init_skills() {
    skill_loader_.discover();
    for (auto& def : tools::builtin_skill_definitions()) {
        skill_loader_.add_skill(def);
    }
}

void Runtime::init_mcp() {
    if (!settings_.mcp_servers.empty()) {
        tools_.mcp_->load_servers(settings_.mcp_servers);
        for (const auto& tool_def : tools_.mcp_->all_tool_definitions()) {
            std::string raw_name(tool_def.name);
            std::string mcp_name = "mcp_" + raw_name;
            auto mgr = tools_.mcp_;
            tools_.registry_.register_tool(
                mcp_name,
                tool_def.description,
                tool_def.parameters,
                [mgr, raw_name](const Json& args) -> std::string {
                    return mgr->execute_tool(raw_name, args);
                });
        }
    }
}

void Runtime::init_workflow() {
    orch_.templates_->register_template(workflow::templates::code_review());
    orch_.templates_->register_template(workflow::templates::documentation());
    orch_.templates_->register_template(workflow::templates::refactoring());
    orch_.templates_->register_template(workflow::templates::test_generation());
}

void Runtime::init_sub_agent() {
    sub_agent_runtime_ = std::make_shared<SubAgentRuntime>(
        settings_, provider_, tools_.registry_);

    auto sub = sub_agent_runtime_;
    tools_.registry_.register_tool(
        std::string("delegate_to_sub_agent"),
        std::string("Delegate a task to a sub-agent. Use for parallelizable subtasks "
            "like searching multiple directories or linting multiple files simultaneously. "
            "Each sub-agent runs independently with filtered tools and returns summarized results."),
        {
            {std::string("prompts"), capabilities::tool::ToolParameterSchema{
                .type = std::string("array"),
                .description = std::string("List of task prompts, one per sub-agent. Each runs in parallel.")
            }},
            {std::string("max_parallel"), capabilities::tool::ToolParameterSchema{
                .type = std::string("integer"),
                .description = std::string("Maximum sub-agents to run concurrently (default: 5)")
            }}
        },
        [sub](const Json& args) -> std::string {
            std::vector<std::string> prompts;
            if (args.contains("prompts") && args["prompts"].is_array()) {
                for (const auto& p : args["prompts"]) {
                    prompts.push_back(p.get<std::string>());
                }
            }
            if (prompts.empty()) {
                return Json{{"success", false}, {"error", "prompts array is empty"}}.dump();
            }
            int max_parallel = args.value("max_parallel", sub->default_config().max_parallel);

            auto config = sub->default_config();
            std::vector<SubAgentRuntime::Result> results;
            try {
                results = sub->execute_parallel(sub->loop(), prompts, config, max_parallel);
            } catch (...) {
                throw;
            }

            Json output = Json::array();
            for (const auto& r : results) {
                output.push_back({{"success", r.success}, {"output", r.output},
                                  {"tool_calls", r.tool_calls}});
            }
            return Json{{"results", output}, {"total", (int)results.size()}}.dump();
        });

    log::info_fmt("init: sub_agent (max_parallel={})", sub->default_config().max_parallel);
}

void Runtime::init_plugins() {
    auto dir = settings_.plugins_dir;
    if (dir.empty()) {
        dir = std::filesystem::path(base::platform::os::data_directory()) / "plugins";
    }
    if (std::filesystem::exists(dir)) {
        orch_.plugin_loader_ = std::make_unique<plugins::PluginLoader>(dir);
        auto [loaded, errors] = orch_.plugin_loader_->load_all();
        if (loaded > 0) {
            log::info_fmt("plugins: loaded {} plugin(s)", loaded);
        }
        for (const auto& err : errors) {
            log::error_fmt("plugins: failed to load: {}", err);
        }

        for (const auto& plugin : orch_.plugin_loader_->loaded_plugins()) {
            for (const auto& tool : plugin.tools) {
                register_plugin_tool(tool);
            }
        }
    }
}

void Runtime::init_capabilities() {
    auto instances = capabilities::CapabilityRegistry::instance().create_all(ws_ctx_);
    for (auto& cap : instances) {
        log::info_fmt("capability: initializing '{}'", cap->name());
        cap->init();
    }
    capabilities_ = std::move(instances);
}

void Runtime::register_plugin_tool(const plugins::BenGearTool& tool) {
    auto tool_name = std::string(tool.name);

    if (tools_.registry_.has_tool(std::string_view(tool_name.data(), tool_name.size()))) {
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
                    schema.enum_values.push_back(
                        v.get<std::string>());
                }
            }
            params.emplace_back(
                p.value("name", ""),
                std::move(schema));
        }
    }

    auto* exec_fn = tool.execute;
    tools_.registry_.register_tool(
        tool_name,
        std::string(tool.description),
        params,
        [exec_fn](const Json& args) -> std::string {
            auto result = exec_fn(args.dump().c_str());
            return std::string(result);
        });

    log::info_fmt("plugins: registered tool '{}'", tool.name);
}

workflow::WorkflowResources Runtime::make_workflow_resources() {
    auto self = shared_from_this();
    std::weak_ptr<Runtime> weak_self = self;
    workflow::WorkflowResources res;
    res.tools = &tools_.registry_;
    res.settings = &settings_;
    res.wf_context = infra_.wf_context.get();
    res.lifetime_context = {};

    res.run_chat_async = [weak_self](net::EventLoop& loop,
                                      const std::string& session_id,
                                      std::string prompt,
                                      std::string model_override)
        -> net::Task<llm::ChatResult> {
        (void)session_id;
        auto locked = weak_self.lock();
        if (!locked) {
            co_return llm::ChatResult::internal_error(
                std::string("workflow resources expired"));
        }
        llm::ConversationHistory history;
        auto& sp = locked->settings_.agent.system_prompt;
        history.set_system_prompt(sp.empty()
            ? std::string("You are a helpful assistant.")
            : std::string(sp.data(), sp.size()));
        history.add_user(std::string_view(prompt.data(), prompt.size()));
        std::string model(model_override.data(), model_override.size());
        auto result = co_await locked->provider().chat_with_tools_async(
            loop, history, locked->tools(), {}, net::CancellationToken{},
            model.empty() ? std::string() : model_override);
        co_return llm::ChatResult::ok(
            std::string(result.dump()),
            std::string(result.dump()));
    };
    return res;
}

workspace::SessionDeps Runtime::make_session_deps() const {
    return workspace::SessionDeps{
        .ws_ctx = ws_ctx_,
        .memory_store = memory_.store_,
        .context_builder = memory_.builder_.get(),
        .thread_pool = infra_.core_pool
    };
}

std::unique_ptr<workspace::Session> Runtime::make_session(std::string session_id) {
    auto session = std::make_unique<workspace::Session>(
        workspace::SessionConfig{
            session_id, settings_.context_length, settings_.context_prune,
            agent::SessionType::main, {}
        },
        make_session_deps(), tools_mut());
    if (!session_id.empty()) {
        session->restore_from_db(history_db());
    } else {
        auto ws_name = ws_ctx_.workspace_name.empty()
            ? std::string("default") : ws_ctx_.workspace_name;
        history_db().create_session(ws_name, session->session_id(),
            std::string(), agent::SessionType::main);
    }
    return session;
}

void Runtime::register_tool(
    const std::string& name,
    const std::string& description,
    const std::vector<std::pair<std::string, capabilities::tool::ToolParameterSchema>>& parameters,
    capabilities::tool::ToolExecutor executor) {
    tools_.registry_.register_tool(name, description, parameters, std::move(executor));
}

application::RequestContext Runtime::request_context() const {
    application::RequestContext request;
    request.username = ws_ctx_.username;
    request.workspace_name = ws_ctx_.workspace_name;
    request.session_id = ws_ctx_.session_id;
    return request;
}

std::shared_ptr<application::WorkspaceResolver> Runtime::make_workspace_resolver() const {
    return std::make_shared<application::WorkspaceResolver>(application::WorkspaceResolverConfig{
        ws_ctx_.tier_paths.global_dir,
        ws_ctx_.workspace_name.empty()
            ? std::string("default")
            : ws_ctx_.workspace_name,
        ws_ctx_.project_path});
}

} // namespace ben_gear::agent::runtime
