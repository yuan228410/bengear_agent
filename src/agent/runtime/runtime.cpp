#include "agent/runtime/runtime.hpp"
#include "agent/core/interface/agent_core.hpp"

#include <filesystem>
#include <fstream>
#include <memory>

namespace ben_gear::agent::runtime {

Runtime::Runtime(config::Settings settings, workspace::WorkspaceContext ws_ctx)
    : settings_(std::move(settings)),
      provider_(settings_),
      tools_(llm::ToolRegistry()),
      ws_ctx_(std::move(ws_ctx)),
      mcp_manager_(settings_.mcp.read_buffer_size),
      core_pool_(std::make_shared<base::concurrency::ThreadPool>(
          base::concurrency::to_thread_pool_config(settings_.thread_pool))),
      io_context_(std::make_shared<net::IoContext>("io")),
      wf_context_(std::make_shared<net::IoContext>("workflow")),
      util_context_(std::make_shared<net::IoContext>("util")),
      workflow_engine_(std::make_shared<workflow::WorkflowEngine>(
          workflow::WorkflowResources{}, nullptr)),
      template_lib_(std::make_shared<workflow::WorkflowTemplateLibrary>()),
      max_tool_steps_(settings_.agent.max_tool_steps),
      max_tool_calls_(settings_.agent.max_tool_calls),
      max_tool_calls_per_step_(settings_.agent.max_tool_calls_per_step) {
}

Runtime::~Runtime() = default;

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
}

// ════════════════════════════════════════════════════════════════════
//  init_* 方法实现
// ════════════════════════════════════════════════════════════════════

void Runtime::init_http_workflow() {
    mcp_manager_.set_io_context(util_context_.get());
    // Register HTTP tools, bind workflow resources
    workflow_engine_->bind_resources(make_workflow_resources());
}

void Runtime::init_workspace() {
    ws_manager_ = std::make_shared<workspace::WorkspaceManager>(
        ws_ctx_.tier_paths.user_dir);
}

void Runtime::init_memory() {
    // Memory store initialization
    // TODO: implement properly with memory::MemoryStore
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

    // Register all tools
    // tools::register_all_tools(...);
    // tools::register_patch_tools(...);
    // tools::register_git_tools(...);
    // etc.
}

void Runtime::init_skills() {
    // Skill loader setup
}

void Runtime::init_mcp() {
    // MCP server setup from settings
    if (!settings_.mcp_servers.empty()) {
        mcp_manager_.load_servers(settings_.mcp_servers);
    }
}

void Runtime::init_workflow() {
    template_lib_->register_template(workflow::templates::code_review());
    template_lib_->register_template(workflow::templates::documentation());
    template_lib_->register_template(workflow::templates::refactoring());
    template_lib_->register_template(workflow::templates::test_generation());
}

void Runtime::init_sub_agent() {
    // SubAgentRuntime creation
}

void Runtime::init_plugins() {
    if (!settings_.plugins_dir.empty() &&
        std::filesystem::exists(settings_.plugins_dir)) {
        plugin_loader_ = std::make_unique<plugins::PluginLoader>(settings_.plugins_dir);
        plugin_loader_->load_all();
    }
}

// ════════════════════════════════════════════════════════════════════
//  辅助方法
// ════════════════════════════════════════════════════════════════════

workflow::WorkflowResources Runtime::make_workflow_resources() {
    workflow::WorkflowResources res;
    res.tools = &tools_;
    res.settings = &settings_;
    res.wf_context = wf_context_.get();
    return res;
}

workspace::SessionDeps Runtime::make_session_deps() const {
    return workspace::SessionDeps{
        .ws_ctx = ws_ctx_,
        .memory_store = memory_store_,
        .context_builder = context_builder_.get(),
        .thread_pool = core_pool_
    };
}

void Runtime::register_tool(
    const container::String& name,
    const container::String& description,
    const container::Vector<std::pair<container::String, llm::ToolParameterSchema>>& parameters,
    llm::ToolExecutor executor) {
    tools_.register_tool(name, description, parameters, std::move(executor));
}

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
        return Json{{"success", true}, {"skipped", true}, {"reason", "command_pipeline"}};
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
    // Simplified checkpoint path extraction
    std::set<std::string> paths;
    auto add_path = [&](const std::string& path) {
        auto normalized = normalize_checkpoint_path(path);
        if (!normalized.empty()) paths.insert(std::move(normalized));
    };

    const std::string name(tool_name);
    if (name == "write_file" || name == "delete_file" || name == "mkdir") {
        add_path(arguments.value("path", ""));
    } else if (name == "rename_file") {
        add_path(arguments.value("src", ""));
        add_path(arguments.value("dst", ""));
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

} // namespace ben_gear::agent::runtime
