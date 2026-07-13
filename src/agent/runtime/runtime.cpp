#include "agent/runtime/runtime.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <set>

#include "base/log/logger.hpp"

#include "llm/mcp/mcp_client.hpp"

#include "capabilities/audit/audit_store.hpp"

#include "tool/acp/core/message.hpp"
#include "tool/skill_tools.hpp"
#include "tool/memory_tools.hpp"
#include "tool/workspace_tools.hpp"
#include "tool/history_tools.hpp"
#include "tool/patch_tools.hpp"
#include "tool/git_tools.hpp"
#include "tool/checkpoint_tools.hpp"
#include "tool/test_loop_tools.hpp"
#include "tool/permission_tools.hpp"
#include "tool/repo_map_tools.hpp"
#include "tool/code_intel_tools.hpp"
#include "tool/diagnostic_context_tools.hpp"
#include "tool/diagnostic_repair_tools.hpp"
#include "tool/workflow_tools.hpp"
#include "tool/builtin_tools.hpp"

namespace ben_gear::agent::runtime {

Runtime::Runtime(config::Settings settings, workspace::WorkspaceContext ws_ctx)
    : settings_(std::move(settings)),
      provider_(settings_),
      tools_(llm::ToolRegistry()),
      ws_ctx_(std::move(ws_ctx)),
      mcp_manager_(std::make_shared<mcp::MCPManager>(settings_.mcp.read_buffer_size)),
      core_pool_(std::make_shared<base::concurrency::ThreadPool>(
          base::concurrency::to_thread_pool_config(settings_.thread_pool))),
      io_context_(std::make_shared<net::IoContext>("io")),
      wf_context_(std::make_shared<net::IoContext>("workflow")),
      util_context_(std::make_shared<net::IoContext>("util")),
      workflow_engine_(std::make_shared<workflow::WorkflowEngine>(
          workflow::WorkflowResources{}, nullptr)),
      template_lib_(std::make_shared<workflow::WorkflowTemplateLibrary>()),
      skill_loader_(skill::make_skill_loader(ws_ctx_.tier_paths)),
      max_tool_steps_(settings_.agent.max_tool_steps),
      max_tool_calls_(settings_.agent.max_tool_calls),
      max_tool_calls_per_step_(settings_.agent.max_tool_calls_per_step) {
}

Runtime::~Runtime() = default;

workspace::HistoryDB& Runtime::history_db() noexcept { return *history_db_; }

void Runtime::post_init() {
    init_all();
}

void Runtime::init_all() {
    init_http_workflow();
    init_workspace();
    init_memory();
    init_history();
    init_tools();
    init_skills();
    init_mcp();
    init_workflow();
    init_sub_agent();
    init_plugins();

    // 注入最小核心 Agent 服务
    agent_.set_file(core::make_default_file_service());
    agent_.set_web(core::make_default_web_service());
    agent_.set_skill(core::make_default_skill_service());
    agent_.set_cmd(core::make_default_command_executor());
    agent_.set_mcp(core::make_default_mcp_service());
}

// ════════════════════════════════════════════════════════════════════
//  init_*  方法
// ════════════════════════════════════════════════════════════════════

void Runtime::init_http_workflow() {
    mcp_manager_->set_io_context(util_context_.get());
    tools::register_http_tools(tools_, *util_context_);
    workflow_engine_->bind_resources(make_workflow_resources());
    tools::register_workflow_tools_with_resources(tools_, workflow_engine_, template_lib_);
}

void Runtime::init_workspace() {
    ws_manager_ = std::make_shared<workspace::WorkspaceManager>(
        ws_ctx_.tier_paths.user_dir);
}

void Runtime::init_memory() {
    memory_store_ = std::make_shared<memory::MemoryStore>(ws_ctx_.tier_paths);
    ensure_default_memory_files();
    context_builder_ = std::make_unique<memory::ContextBuilder>(
        *memory_store_, skill_loader_.get_skills_metadata());
    auto project_dir = ws_ctx_.project_path.empty()
        ? settings_.workspace
        : std::filesystem::path(std::string(
            ws_ctx_.project_path.data(), ws_ctx_.project_path.size()));
    context_builder_->set_project_dir(project_dir);
    if (!settings_.agent.system_prompt.empty()) {
        context_builder_->set_core_prompt(
            std::string(settings_.agent.system_prompt.data(),
                        settings_.agent.system_prompt.size()));
    }
    context_builder_->set_inject_project_doc(settings_.agent.inject_project_doc);
}

void Runtime::ensure_default_memory_files() {
    auto soul_path = ws_ctx_.tier_paths.dir(base::Tier::global) / "memory" / "SOUL.md";
    if (!std::filesystem::exists(soul_path)) {
        const char* soul_content =
            "# Soul\n"
            "\n"
            "You are BenGear, an AI coding agent for software engineering tasks.\n"
            "\n"
            "## Core Capabilities\n"
            "- Understand and modify codebases.\n"
            "- Use tools to inspect files, run commands, and verify changes.\n"
            "- Preserve project instructions, workspace context, and user-approved constraints.\n";
        memory_store_->write_soul(
            container::String(soul_content, std::strlen(soul_content)),
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
    history_db_ = std::make_unique<workspace::HistoryDB>(db_path);
}

void Runtime::init_tools() {
    policy_engine_ = std::make_shared<permission::PolicyEngine>(ws_ctx_);
    patch_service_ = std::make_shared<patch::PatchService>(ws_ctx_);
    auto pw_resolver = std::make_shared<application::WorkspaceResolver>(
        make_workspace_resolver());
    patch_workspace_resolver_ = pw_resolver;
    patch_use_cases_ = std::make_shared<application::PatchUseCases>(
        *pw_resolver, make_command_pipeline());
    git_service_ = std::make_shared<git::GitService>(ws_ctx_);
    checkpoint_service_ = std::make_shared<checkpoint::CheckpointService>(ws_ctx_);
    test_loop_service_ = std::make_shared<test_loop::TestLoopService>(ws_ctx_);
    workspace_index_service_ = std::make_shared<workspace_index::WorkspaceIndexService>(ws_ctx_);
    repo_map_service_ = std::make_shared<repo_map::RepoMapService>(
        ws_ctx_, git_service_, test_loop_service_, workspace_index_service_);
    code_intel_service_ = std::make_shared<code_intel::CodeIntelService>(
        ws_ctx_, repo_map_service_);
    diagnostic_context_service_ = std::make_shared<diagnostic_context::DiagnosticContextService>(
        ws_ctx_, code_intel_service_);
    diagnostic_repair_plan_service_ = std::make_shared<diagnostic_repair::DiagnosticRepairPlanService>(
        ws_ctx_, diagnostic_context_service_);
    diagnostic_repair_patch_preview_service_ = std::make_shared<diagnostic_repair::DiagnosticRepairPatchPreviewService>(
        ws_ctx_, diagnostic_repair_plan_service_, patch_service_);

    // 注册全部工具
    tools::register_all_tools(tools_, settings_.agent.command_timeout,
                              &skill_loader_, *util_context_);
    auto pipeline = make_command_pipeline();
    auto request = request_context();
    tools::register_patch_tools(tools_, patch_service_, patch_use_cases_, request);
    tools::register_git_tools(tools_, git_service_, pipeline, request,
                              ws_ctx_.project_path);
    tools::register_checkpoint_tools(tools_, checkpoint_service_, pipeline,
                                     request, ws_ctx_.project_path);
    tools::register_test_loop_tools(tools_, test_loop_service_, pipeline,
                                    request, ws_ctx_.project_path);
    tools::register_repo_map_tools(tools_, repo_map_service_);
    tools::register_code_intel_tools(tools_, code_intel_service_);
    tools::register_diagnostic_context_tools(tools_, diagnostic_context_service_);
    tools::register_diagnostic_repair_tools(
        tools_, diagnostic_repair_plan_service_,
        diagnostic_repair_patch_preview_service_);
    tools::register_permission_tools(tools_, policy_engine_);
    tools::register_memory_tools(tools_, memory_store_);
    tools::register_workspace_tools(tools_, ws_manager_);
    tools::register_history_tools(tools_, *history_db_, ws_ctx_);

    // update_todo 工具在 Agent 层面注册
    tools_.register_tool(
        container::String("update_todo"),
        container::String("Update the session TODO list"),
        {
            {container::String("action"),
             {container::String("string"),
              container::String("set_items, update_item, or clear"),
              {container::String("set_items"), container::String("update_item"),
               container::String("clear")}, true}},
            {container::String("items"),
             {container::String("array"),
              container::String("TODO items for set_items"), {}, false}},
            {container::String("item"),
             {container::String("object"),
              container::String("Single TODO item"), {}, false}},
        },
        [](const Json&) -> container::String {
            return container::String("handled by agent session");
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
        mcp_manager_->load_servers(settings_.mcp_servers);
        for (const auto& tool_def : mcp_manager_->all_tool_definitions()) {
            std::string raw_name(tool_def.name);
            std::string mcp_name = "mcp_" + raw_name;
            auto mgr = mcp_manager_;
            tools_.register_tool(
                container::String(mcp_name.c_str()),
                tool_def.description,
                tool_def.parameters,
                [mgr, raw_name](const Json& args) -> std::string {
                    return mgr->execute_tool(raw_name, args);
                });
        }
    }
}

void Runtime::init_workflow() {
    template_lib_->register_template(workflow::templates::code_review());
    template_lib_->register_template(workflow::templates::documentation());
    template_lib_->register_template(workflow::templates::refactoring());
    template_lib_->register_template(workflow::templates::test_generation());
}

void Runtime::init_sub_agent() {
    sub_agent_runtime_ = std::make_shared<SubAgentRuntime>(
        settings_, provider_, tools_);

    // 注册子代理委派工具
    auto sub = sub_agent_runtime_;
    tools_.register_tool(
        container::String("delegate_to_sub_agent"),
        container::String("Delegate a task to a sub-agent. Use for parallelizable subtasks "
            "like searching multiple directories or linting multiple files simultaneously. "
            "Each sub-agent runs independently with filtered tools and returns summarized results."),
        {
            {container::String("prompts"), llm::ToolParameterSchema{
                .type = container::String("array"),
                .description = container::String("List of task prompts, one per sub-agent. Each runs in parallel.")
            }},
            {container::String("max_parallel"), llm::ToolParameterSchema{
                .type = container::String("integer"),
                .description = container::String("Maximum sub-agents to run concurrently (default: 5)")
            }}
        },
        [sub, this](const Json& args) -> container::String {
            std::vector<std::string> prompts;
            if (args.contains("prompts") && args["prompts"].is_array()) {
                for (const auto& p : args["prompts"]) {
                    prompts.push_back(p.get<std::string>());
                }
            }
            if (prompts.empty()) {
                return container::String(Json{{"success", false}, {"error", "prompts array is empty"}}.dump().c_str());
            }
            int max_parallel = args.value("max_parallel", sub->default_config().max_parallel);

            // 子代理需要独立的 EventLoop（sync_wait 要求 loop 在线程中 run）
            net::EventLoop sub_loop;
            std::thread loop_thread([&] { sub_loop.run(); });
            auto config = sub->default_config();
            std::vector<SubAgentRuntime::Result> results;
            try {
                results = sub->execute_parallel(sub_loop, prompts, config, max_parallel);
            } catch (...) {
                sub_loop.stop();
                loop_thread.join();
                throw;
            }
            sub_loop.stop();
            loop_thread.join();

            Json output = Json::array();
            for (const auto& r : results) {
                output.push_back({{"success", r.success}, {"output", r.output},
                                  {"tool_calls", r.tool_calls}});
            }
            return container::String(Json{{"results", output}, {"total", (int)results.size()}}.dump().c_str());
        });

    log::info_fmt("init: sub_agent (max_parallel={})", sub->default_config().max_parallel);
}

void Runtime::init_plugins() {
    auto dir = settings_.plugins_dir;
    if (dir.empty()) {
        dir = std::filesystem::path(base::platform::os::data_directory()) / "plugins";
    }
    if (std::filesystem::exists(dir)) {
        plugin_loader_ = std::make_unique<plugins::PluginLoader>(dir);
        auto [loaded, errors] = plugin_loader_->load_all();
        if (loaded > 0) {
            log::info_fmt("plugins: loaded {} plugin(s)", loaded);
        }
        for (const auto& err : errors) {
            log::error_fmt("plugins: failed to load: {}", err);
        }

        // 将插件工具注册到 ToolRegistry（LLM 可直接调用）
        for (const auto& plugin : plugin_loader_->loaded_plugins()) {
            for (const auto& tool : plugin.tools) {
                register_plugin_tool(tool);
            }
        }
    }
}

void Runtime::register_plugin_tool(const plugins::BenGearTool& tool) {
    // 解析参数 JSON → ToolParameterSchema
    container::Vector<std::pair<container::String, llm::ToolParameterSchema>> params;
    auto params_json = Json::parse(tool.params_json ? tool.params_json : "[]");
    if (params_json.is_array()) {
        for (const auto& p : params_json) {
            llm::ToolParameterSchema schema;
            schema.type = container::String(p.value("type", "string").c_str());
            schema.description = container::String(p.value("description", "").c_str());
            schema.required = p.value("required", false);
            if (p.contains("enum_values") && p["enum_values"].is_array()) {
                for (const auto& v : p["enum_values"]) {
                    schema.enum_values.push_back(
                        container::String(v.get<std::string>().c_str()));
                }
            }
            params.emplace_back(
                container::String(p.value("name", "").c_str()),
                std::move(schema));
        }
    }

    // 注册到 ToolRegistry
    auto* exec_fn = tool.execute;  // C 函数指针
    tools_.register_tool(
        container::String(tool.name),
        container::String(tool.description),
        params,
        [exec_fn](const Json& args) -> container::String {
            auto result = exec_fn(args.dump().c_str());
            return container::String(result);
        });

    log::info_fmt("plugins: registered tool '{}'", tool.name);
}

// ════════════════════════════════════════════════════════════════════
//  Command Pipeline
// ════════════════════════════════════════════════════════════════════

application::CommandPipeline Runtime::make_command_pipeline() const {
    return application::make_command_pipeline(application::CommandGovernanceConfig{
        [this](const container::String&, const container::String&,
               const container::String&, std::string_view tool_name,
               const Json& arguments) {
            return check_command_permission(tool_name, arguments);
        },
        [this](const application::CommandDescriptor& command) {
            return create_command_checkpoint(command);
        },
        [this](const container::String& workspace,
               const container::String& session_id,
               const container::String& username,
               const container::String& category,
               const container::String& action,
               const Json& details) {
            return append_command_audit(workspace, session_id, username,
                                       category, action, details);
        },
        [this](const container::String& workspace,
               const container::String& session_id,
               const container::String& username,
               const Json& execution) {
            return append_runtime_execution(workspace, session_id, username, execution);
        }});
}

Json Runtime::check_command_permission(std::string_view tool_name,
                                        const Json& arguments) const {
    if (!policy_engine_) {
        return Json{{"success", false},
                    {"error_type", "permission_service_unavailable"},
                    {"message", "permission service unavailable"}};
    }
    auto decision = policy_engine_->evaluate_tool_permission(tool_name, arguments);
    if (decision.allowed()) {
        return Json{{"success", true},
                    {"policy_effect", "allow"},
                    {"policy_key", decision.policy_key}};
    }
    return permission::to_json(decision);
}

domain::AppResult<void> Runtime::create_command_checkpoint(
    const application::CommandDescriptor& command) const {
    if (!checkpoint_service_ || !command.mutates_workspace ||
        command.affected_paths.empty()) {
        return domain::AppResult<void>::success();
    }
    std::vector<std::string> paths;
    for (const auto& path : command.affected_paths)
        paths.emplace_back(path.c_str());
    auto result = checkpoint_service_->create(
        paths, "auto checkpoint before " + std::string(command.action.c_str()));
    if (result.ok())
        return domain::AppResult<void>::success();
    return domain::AppResult<void>::failure(result.error());
}

Json Runtime::append_command_audit(const container::String& workspace,
                                    const container::String& session_id,
                                    const container::String& username,
                                    const container::String& category,
                                    const container::String& action,
                                    const Json& details) const {
    Json event = details;
    event["workspace"] = std::string(workspace.data(), workspace.size());
    event["session_id"] = std::string(session_id.data(), session_id.size());
    event["username"] = std::string(username.data(), username.size());
    event["category"] = std::string(category.data(), category.size());
    event["action"] = std::string(action.data(), action.size());
    audit::AuditStore store(
        ws_ctx_.tier_paths.user_dir / "audit" / "events.jsonl");
    return store.append(std::move(event));
}

Json Runtime::append_runtime_execution(const container::String&,
                                        const container::String&,
                                        const container::String&,
                                        const Json& execution) const {
    audit::RuntimeExecutionStore store(
        ws_ctx_.tier_paths.user_dir / "runtime" / "executions.jsonl");
    return store.append(execution);
}

// ════════════════════════════════════════════════════════════════════
//  Workflow Resources
// ════════════════════════════════════════════════════════════════════

workflow::WorkflowResources Runtime::make_workflow_resources() {
    auto self = shared_from_this();
    std::weak_ptr<Runtime> weak_self = self;
    workflow::WorkflowResources res;
    res.tools = &tools_;
    res.settings = &settings_;
    res.wf_context = wf_context_.get();
    res.lifetime_context = {};

    res.run_chat_async = [weak_self](net::EventLoop& loop,
                                      const std::string& session_id,
                                      container::String prompt,
                                      container::String model_override)
        -> net::Task<llm::ChatResult> {
        auto self = weak_self.lock();
        if (!self) {
            co_return llm::ChatResult::internal_error(
                container::String("workflow resources expired"));
        }
        // 使用 ConversationHistory 直接调用 LLM
        workspace::ConversationHistory history;
        history.set_system_prompt(std::string("You are a helpful assistant."));
        history.add_user(std::string_view(prompt.data(), prompt.size()));
        std::string model(model_override.data(), model_override.size());
        auto result = co_await self->provider().chat_with_tools_async(
            loop, history, self->tools(), {}, net::CancellationToken{},
            model.empty() ? container::String() : model_override);
        co_return llm::ChatResult::ok(
            container::String(result.dump()),
            container::String(result.dump()));
    };
    return res;
}

// ════════════════════════════════════════════════════════════════════
//  Session Deps
// ════════════════════════════════════════════════════════════════════

workspace::SessionDeps Runtime::make_session_deps() const {
    return workspace::SessionDeps{
        .ws_ctx = ws_ctx_,
        .memory_store = memory_store_,
        .context_builder = context_builder_.get(),
        .thread_pool = core_pool_
    };
}

std::unique_ptr<workspace::Session> Runtime::make_session(container::String session_id) {
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
            ? container::String("default") : ws_ctx_.workspace_name;
        history_db().create_session(ws_name, session->session_id(),
            container::String(), agent::SessionType::main);
    }
    return session;
}

void Runtime::register_tool(
    const container::String& name,
    const container::String& description,
    const container::Vector<std::pair<container::String, llm::ToolParameterSchema>>& parameters,
    llm::ToolExecutor executor) {
    tools_.register_tool(name, description, parameters, std::move(executor));
}

// ════════════════════════════════════════════════════════════════════
//  Permission
// ════════════════════════════════════════════════════════════════════

permission::PermissionDecision Runtime::evaluate_tool_permission(
    std::string_view tool_name, const Json& arguments) const {
    if (tool_uses_command_pipeline(tool_name)) {
        permission::PermissionDecision decision;
        decision.effect = permission::PolicyEffect::allow;
        decision.policy_key = "command.pipeline";
        decision.reason = "governed by application command pipeline";
        return decision;
    }
    return policy_engine_
        ? policy_engine_->evaluate_tool_permission(tool_name, arguments)
        : permission::PermissionDecision{};
}

Json Runtime::before_tool_execution(
    std::string_view tool_name, const Json& arguments) const {
    if (tool_uses_command_pipeline(tool_name))
        return Json{{"success", true}, {"skipped", true},
                    {"reason", "command_pipeline"}};
    if (!checkpoint_service_)
        return Json{{"success", true}, {"skipped", true}};
    auto paths = checkpoint_paths_for_tool(tool_name, arguments);
    if (paths.empty())
        return Json{{"success", true}, {"skipped", true}};
    auto result = checkpoint_service_->create(
        paths, "auto checkpoint before " + std::string(tool_name));
    if (!result.ok()) {
        return Json{{"success", false},
                    {"error_type", std::string(result.error().code.c_str())},
                    {"message", std::string(result.error().message.c_str())}};
    }
    return Json{{"success", true},
                {"checkpoint_id", result.value().checkpoint_id},
                {"paths", paths}};
}

bool Runtime::tool_uses_command_pipeline(std::string_view tool_name) const {
    return tool_name == "apply_patch" ||
           tool_name == "revert_patch" ||
           tool_name == "run_tests" ||
           tool_name == "restore_checkpoint" ||
           tool_name == "delete_checkpoint" ||
           tool_name == "git_restore" ||
           tool_name == "git_commit" ||
           tool_name == "git_branch" ||
           tool_name == "git_worktree";
}

std::vector<std::string> Runtime::checkpoint_paths_for_tool(
    std::string_view tool_name, const Json& arguments) const {
    std::set<std::string> paths;
    auto add_path = [&](const std::string& path) {
        auto normalized = normalize_checkpoint_path(path);
        if (!normalized.empty()) paths.insert(std::move(normalized));
    };
    auto add_paths_array = [&](const Json& value) {
        if (!value.is_array()) return;
        for (const auto& item : value)
            if (item.is_string()) add_path(item.get<std::string>());
    };

    const std::string name(tool_name);
    if (name == "write_file" || name == "delete_file" || name == "mkdir") {
        add_path(arguments.value("path", ""));
    } else if (name == "rename_file") {
        add_path(arguments.value("src", ""));
        add_path(arguments.value("dst", ""));
    } else if (name == "copy_file") {
        add_path(arguments.value("dst", ""));
    } else if (name == "apply_patch") {
        if (patch_service_) {
            auto preview = patch_service_->preview(
                arguments.value("unified_diff", ""));
            if (preview.success) {
                for (const auto& file : preview.files) {
                    auto path = file.kind == patch::FileChangeKind::remove
                        ? file.old_path : file.new_path;
                    add_path(path.generic_string());
                }
            }
        }
    } else if (name == "revert_patch") {
        if (patch_service_) {
            auto change = patch_service_->read_change(
                arguments.value("change_id", ""));
            if (change.ok()) {
                for (const auto& file : change.value().change.files)
                    add_path(file.path);
            }
        }
    } else if (name == "restore_checkpoint") {
        if (arguments.contains("paths") && arguments["paths"].is_array() &&
            !arguments["paths"].empty()) {
            add_paths_array(arguments["paths"]);
        } else if (checkpoint_service_) {
            auto checkpoint = checkpoint_service_->read(
                arguments.value("checkpoint_id", ""));
            if (checkpoint.ok()) {
                for (const auto& file : checkpoint.value().checkpoint.files)
                    add_path(file.path);
            }
        }
    } else if (name == "git_restore") {
        if (arguments.contains("paths"))
            add_paths_array(arguments["paths"]);
    }

    return std::vector<std::string>(paths.begin(), paths.end());
}

std::string Runtime::normalize_checkpoint_path(const std::string& input) const {
    if (input.empty()) return {};
    std::error_code ec;
    auto root = ws_ctx_.project_path.empty()
        ? std::filesystem::current_path(ec)
        : std::filesystem::path(std::string(
            ws_ctx_.project_path.data(), ws_ctx_.project_path.size()));
    if (ec) return {};
    std::filesystem::path path(input);
    if (path.is_absolute()) {
        auto rel = std::filesystem::relative(path, root, ec);
        if (ec) return {};
        path = rel;
    }
    auto generic = path.lexically_normal().generic_string();
    if (generic.empty() || generic == "." || generic == ".." ||
        generic.rfind("../", 0) == 0 ||
        generic.find("/../") != std::string::npos) {
        return {};
    }
    return generic;
}

application::RequestContext Runtime::request_context() const {
    application::RequestContext request;
    request.username = ws_ctx_.username;
    request.workspace_name = ws_ctx_.workspace_name;
    request.session_id = ws_ctx_.session_id;
    return request;
}

application::WorkspaceResolver Runtime::make_workspace_resolver() const {
    return application::WorkspaceResolver(application::WorkspaceResolverConfig{
        ws_ctx_.tier_paths.global_dir,
        ws_ctx_.workspace_name.empty()
            ? container::String("default")
            : ws_ctx_.workspace_name,
        ws_ctx_.project_path});
}

container::String Runtime::session_id_for_sub_agent() const {
    return ws_ctx_.session_id;
}

// ════════════════════════════════════════════════════════════════════
//  SubAgentRuntime — 并行子代理执行（单轮 LLM 调用，无工具循环）
// ════════════════════════════════════════════════════════════════════

Runtime::SubAgentRuntime::SubAgentRuntime(
    const config::Settings& settings,
    llm::ProviderClient& provider,
    const llm::ToolRegistry& tools)
    : default_config_(settings.agent.sub_agent),
      settings_(settings),
      provider_(provider),
      tools_(tools) {}

Runtime::SubAgentRuntime::Result
Runtime::SubAgentRuntime::execute(net::EventLoop& loop,
                                   std::string_view prompt,
                                   const agent::SubAgentConfig& config) {
    Result result;
    auto start = std::chrono::steady_clock::now();

    try {
        workspace::ConversationHistory history;
        history.set_system_prompt(
            "You are a sub-agent. Answer concisely with only the essential information.");
        history.add_user(std::string_view(prompt.data(), prompt.size()));

        auto response = net::sync_wait(loop,
            provider_.chat_with_tools_async(loop, history, tools_, {}, {}));

        // 提取文本
        if (response.contains("choices") && response["choices"].is_array() &&
            !response["choices"].empty()) {
            auto msg = response["choices"][0]["message"];
            if (msg.contains("content") && !msg["content"].is_null()) {
                auto text = Json(msg["content"]).get<std::string>();
                result.output = text;
            } else if (msg.contains("tool_calls")) {
                result.output = "(sub-agent issued tool calls)";
            }
        }

        // 自动摘要
        if (config.auto_summary && static_cast<int>(result.output.size()) > config.max_output_chars) {
            result.output = result.output.substr(0, static_cast<size_t>(config.max_output_chars))
                          + "\n...[truncated]";
        }
        result.success = true;
    } catch (const std::exception& e) {
        result.success = false;
        result.output = std::string("sub_agent error: ") + e.what();
    }

    result.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    return result;
}

std::vector<Runtime::SubAgentRuntime::Result>
Runtime::SubAgentRuntime::execute_parallel(
    net::EventLoop& loop,
    const std::vector<std::string>& prompts,
    const agent::SubAgentConfig& config,
    int max_parallel) {

    if (prompts.empty()) return {};
    if (max_parallel <= 0) max_parallel = 1;

    std::vector<Result> results(prompts.size());
    std::atomic<size_t> next{0};

    auto worker = [&]() {
        for (;;) {
            size_t i = next.fetch_add(1, std::memory_order_acq_rel);
            if (i >= prompts.size()) break;
            results[i] = execute(loop, prompts[i], config);
        }
    };

    int workers = std::min(max_parallel, static_cast<int>(prompts.size()));
    std::vector<std::thread> threads;
    for (int w = 0; w < workers; ++w) {
        threads.emplace_back(worker);
    }
    for (auto& t : threads) t.join();

    return results;
}

} // namespace ben_gear::agent::runtime

