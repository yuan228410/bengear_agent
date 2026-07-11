#pragma once

#include "base/config/settings.hpp"
#include "llm/provider_client.hpp"
#include "tool/registry.hpp"
#include "tool/types.hpp"
#include "llm/skill/skill.hpp"
#include "memory/store.hpp"
#include "application/command_governance.hpp"
#include "application/patch_use_cases.hpp"
#include "application/workspace_resolver.hpp"
#include "capabilities/audit/audit_store.hpp"
#include "base/domain/errors.hpp"

#include "memory/context.hpp"
#include "workspace/history_db.hpp"
#include "workspace/types.hpp"
#include "workspace/manager.hpp"
#include "llm/mcp/mcp_client.hpp"
#include "tool/skill_tools.hpp"
#include "tool/memory_tools.hpp"
#include "tool/workspace_tools.hpp"
#include "tool/history_tools.hpp"
#include "tool/sub_agent_tools.hpp"
#include "tool/patch_tools.hpp"
#include "tool/git_tools.hpp"
#include "tool/checkpoint_tools.hpp"
#include "tool/test_loop_tools.hpp"
#include "tool/permission_tools.hpp"
#include "tool/repo_map_tools.hpp"
#include "tool/code_intel_tools.hpp"
#include "tool/diagnostic_context_tools.hpp"
#include "tool/diagnostic_repair_tools.hpp"
#include "capabilities/permission/policy_engine.hpp"
#include "intelligence/repo_map/repo_map_service.hpp"
#include "intelligence/code_intel/code_intel_service.hpp"
#include "intelligence/diagnostic_context/diagnostic_context_service.hpp"
#include "intelligence/diagnostic_repair/diagnostic_repair_patch_preview_service.hpp"
#include "intelligence/diagnostic_repair/diagnostic_repair_plan_service.hpp"
#include "workflow/workflow_engine.hpp"
#include "workflow/workflow_templates.hpp"
#include "base/concurrency/thread_pool.hpp"
#include "net/io_context.hpp"
#include "base/log/logger.hpp"
#include "intelligence/workspace_index/workspace_index_service.hpp"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace ben_gear::agent {

namespace container = base::container;

/// 共享资源 — 按 (用户, 工作空间) 构建一次，多 Agent/多会话复用
///
/// 线程安全保证：
/// - 所有 const 访问器线程安全（不可变或内部同步）
/// - register_tool() 线程安全（ToolRegistry 内部 shared_mutex）
/// - history_db() 返回内部同步对象
/// - mcp_manager() 返回内部同步对象
/// - core_pool() 返回核心调度线程池，多 Agent 共用
class SharedResources : public std::enable_shared_from_this<SharedResources>,
                        public permission::ToolPermissionProvider {
public:
    explicit SharedResources(config::Settings settings,
                             workspace::WorkspaceContext ws_ctx)
        : settings_(std::move(settings)),
          provider_(settings_),
          tools_(llm::ToolRegistry()),
          ws_ctx_(std::move(ws_ctx)),
          skill_loader_(skill::make_skill_loader(ws_ctx_.tier_paths)),
          mcp_manager_(settings_.mcp.read_buffer_size),
          core_pool_(std::make_shared<base::concurrency::ThreadPool>(
              base::concurrency::to_thread_pool_config(settings_.thread_pool))),
          io_context_(std::make_shared<net::IoContext>("io")),
          wf_context_(std::make_shared<net::IoContext>("workflow")),
          util_context_(std::make_shared<net::IoContext>("util")),
          workflow_engine_(std::make_shared<workflow::WorkflowEngine>(workflow::WorkflowResources{}, nullptr)),
          template_lib_(std::make_shared<workflow::WorkflowTemplateLibrary>()),
          max_tool_steps_(settings_.agent.max_tool_steps),
          max_tool_calls_(settings_.agent.max_tool_calls),
          max_tool_calls_per_step_(settings_.agent.max_tool_calls_per_step) {
        init();
    }

    // --- 访问器（全部线程安全）---

    const config::Settings& settings() const noexcept { return settings_; }
    llm::ProviderClient& provider() noexcept { return provider_; }
    const llm::ToolRegistry& tools() const noexcept { return tools_; }
    llm::ToolRegistry& tools_mut() noexcept { return tools_; }
    const skill::SkillLoader& skill_loader() const noexcept { return skill_loader_; }
    const std::shared_ptr<memory::MemoryStore>& memory_store() const noexcept { return memory_store_; }
    const std::unique_ptr<memory::ContextBuilder>& context_builder() const noexcept { return context_builder_; }
    workspace::HistoryDB& history_db() noexcept { return *history_db_; }
    const std::shared_ptr<workspace::WorkspaceManager>& workspace_manager() const noexcept { return ws_manager_; }
    mcp::MCPManager& mcp_manager() noexcept { return mcp_manager_; }
    const workspace::WorkspaceContext& workspace_context() const noexcept { return ws_ctx_; }
    int max_tool_steps() const noexcept { return max_tool_steps_; }
    int max_tool_calls() const noexcept { return max_tool_calls_; }
    int max_tool_calls_per_step() const noexcept { return max_tool_calls_per_step_; }

    const std::shared_ptr<base::concurrency::ThreadPool>& core_pool() const noexcept { return core_pool_; }
    const std::shared_ptr<workflow::WorkflowEngine>& workflow_engine() const noexcept { return workflow_engine_; }
    const std::shared_ptr<net::IoContext>& io_context() const noexcept { return io_context_; }
    const std::shared_ptr<net::IoContext>& wf_context() const noexcept { return wf_context_; }
    const std::shared_ptr<net::IoContext>& util_context() const noexcept { return util_context_; }
    const std::shared_ptr<workflow::WorkflowTemplateLibrary>& template_lib() const noexcept { return template_lib_; }
 const std::shared_ptr<SubAgentRuntime>& sub_agent_runtime() const noexcept { return sub_agent_runtime_; }
    const std::shared_ptr<permission::PolicyEngine>& policy_engine() const noexcept { return policy_engine_; }

    permission::PermissionDecision evaluate_tool_permission(std::string_view tool_name,
                                                            const Json& arguments) const override {
        if (tool_uses_command_pipeline(tool_name)) {
            permission::PermissionDecision decision;
            decision.effect = permission::PolicyEffect::allow;
            decision.policy_key = "command.pipeline";
            decision.reason = "governed by application command pipeline";
            return decision;
        }
        return policy_engine_ ? policy_engine_->evaluate_tool_permission(tool_name, arguments)
                              : permission::PermissionDecision{};
    }

    Json before_tool_execution(std::string_view tool_name, const Json& arguments) const override {
        if (tool_uses_command_pipeline(tool_name)) return Json{{"success", true}, {"skipped", true}, {"reason", "command_pipeline"}};
        if (!checkpoint_service_) return Json{{"success", true}, {"skipped", true}};
        auto paths = checkpoint_paths_for_tool(tool_name, arguments);
        if (paths.empty()) return Json{{"success", true}, {"skipped", true}};
        auto result = checkpoint_service_->create(paths, "auto checkpoint before " + std::string(tool_name));
        if (!result.ok()) {
            return Json{{"success", false},
                        {"error_type", std::string(result.error().code.c_str())},
                        {"message", std::string(result.error().message.c_str())}};
        }
        return Json{{"success", true}, {"checkpoint_id", result.value().checkpoint_id}, {"paths", paths}};
    }

    /// 创建 Session 依赖
    workspace::SessionDeps make_session_deps() const {
        return workspace::SessionDeps{
            .ws_ctx = ws_ctx_,
            .memory_store = memory_store_,
            .context_builder = context_builder_.get(),
            .thread_pool = core_pool_
        };
    }

    /// 注册自定义工具
    void register_tool(const container::String& name,
                      const container::String& description,
                      const container::Vector<std::pair<container::String, llm::ToolParameterSchema>>& parameters,
                      llm::ToolExecutor executor) {
        tools_.register_tool(name, description, parameters, std::move(executor));
    }

    /// 创建工作流资源（声明在头文件，实现在 .cpp，避免 agent.hpp 循环依赖）
    workflow::WorkflowResources make_workflow_resources();

    /// 延迟初始化（需要 shared_from_this，必须在 shared_ptr 构造后调用）
    void post_init() {
        init_http_workflow();
        init_workspace();
        init_memory();
        init_history();
        init_tools();
        init_skills();
        init_mcp();
        init_workflow();
        init_sub_agent();
    }

private:
    std::vector<std::string> checkpoint_paths_for_tool(std::string_view tool_name, const Json& arguments) const {
        std::set<std::string> paths;
        auto add_path = [&](const std::string& path) {
            auto normalized = normalize_checkpoint_path(path);
            if (!normalized.empty()) paths.insert(std::move(normalized));
        };
        auto add_paths_array = [&](const Json& value) {
            if (!value.is_array()) return;
            for (const auto& item : value) {
                if (item.is_string()) add_path(item.get<std::string>());
            }
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
                auto preview = patch_service_->preview(arguments.value("unified_diff", ""));
                if (preview.success) {
                    for (const auto& file : preview.files) {
                        auto path = file.kind == patch::FileChangeKind::remove ? file.old_path : file.new_path;
                        add_path(path.generic_string());
                    }
                }
            }
        } else if (name == "revert_patch") {
            if (patch_service_) {
                auto change = patch_service_->read_change(arguments.value("change_id", ""));
                if (change.ok()) {
                    for (const auto& file : change.value().change.files) add_path(file.path);
                }
            }
        } else if (name == "restore_checkpoint") {
            if (arguments.contains("paths") && arguments["paths"].is_array() && !arguments["paths"].empty()) {
                add_paths_array(arguments["paths"]);
            } else if (checkpoint_service_) {
                auto checkpoint = checkpoint_service_->read(arguments.value("checkpoint_id", ""));
                if (checkpoint.ok()) {
                    for (const auto& file : checkpoint.value().checkpoint.files) add_path(file.path);
                }
            }
        } else if (name == "git_restore") {
            if (arguments.contains("paths")) add_paths_array(arguments["paths"]);
        }

        return std::vector<std::string>(paths.begin(), paths.end());
    }

    std::string normalize_checkpoint_path(const std::string& input) const {
        if (input.empty()) return {};
        std::error_code ec;
        auto root = ws_ctx_.project_path.empty()
            ? std::filesystem::current_path(ec)
            : std::filesystem::path(std::string(ws_ctx_.project_path.data(), ws_ctx_.project_path.size()));
        if (ec) return {};

        std::filesystem::path path(input);
        if (path.is_absolute()) {
            auto rel = std::filesystem::relative(path, root, ec);
            if (ec) return {};
            path = rel;
        }
        auto generic = path.lexically_normal().generic_string();
        if (generic.empty() || generic == "." || generic == ".." || generic.rfind("../", 0) == 0 || generic.find("/../") != std::string::npos) {
            return {};
        }
        return generic;
    }

    void init() {
        log::debug_fmt("init: SharedResources");
    }

    void init_http_workflow() {
        log::debug_fmt("init: http + workflow");
        mcp_manager_.set_io_context(util_context_.get());
        tools::register_http_tools(tools_, *util_context_);
        // 绑定工作流资源（需要 SharedResources 完全初始化后才能绑定）
        workflow_engine_->bind_resources(make_workflow_resources());
        tools::register_workflow_tools_with_resources(tools_, workflow_engine_, template_lib_);
        log::info_fmt("http + workflow tools registered with SharedResources");
    }

    void init_workspace() {
        log::debug_fmt("init: workspace");
        ws_manager_ = std::make_shared<workspace::WorkspaceManager>(ws_ctx_.tier_paths.user_dir);
    }

    void init_memory() {
        log::debug_fmt("init: memory");
        memory_store_ = std::make_shared<memory::MemoryStore>(ws_ctx_.tier_paths);

        ensure_default_memory_files();

        context_builder_ = std::make_unique<memory::ContextBuilder>(*memory_store_, skill_loader_.get_skills_metadata());
        const auto project_dir = ws_ctx_.project_path.empty()
            ? settings_.workspace
            : std::filesystem::path(std::string(ws_ctx_.project_path.data(), ws_ctx_.project_path.size()));
        context_builder_->set_project_dir(project_dir);
        log::info_fmt("SharedResources: context project_dir={} user={} workspace={}",
                      project_dir.string(), ws_ctx_.username.c_str(), ws_ctx_.workspace_name.c_str());
        if (!settings_.agent.system_prompt.empty()) {
            context_builder_->set_core_prompt(
                std::string(settings_.agent.system_prompt.data(),
                            settings_.agent.system_prompt.size()));
        }
    }

    /// 缺失时写入最小默认记忆文件，只保留核心能力相关内容。
    void ensure_default_memory_files() {
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
            log::info_fmt("init: created default SOUL.md");
        }

        auto user_dir = ws_ctx_.tier_paths.dir(base::Tier::user) / "memory";
        std::filesystem::create_directories(user_dir);
        auto user_path = user_dir / "USER.md";
        if (!std::filesystem::exists(user_path)) {
            auto username = std::string(ws_ctx_.username.data(), ws_ctx_.username.size());
            std::string user_content =
                "# User\n"
                "\n"
                "Username: " + username + "\n";
            std::ofstream file(user_path, std::ios::binary);
            if (file) {
                file.write(user_content.data(), static_cast<std::streamsize>(user_content.size()));
                log::info_fmt("init: created default USER.md");
            }
        }
    }

    void init_history() {
        log::debug_fmt("init: history");
        auto db_path = ws_ctx_.tier_paths.user_dir / "history.db";
        history_db_ = std::make_unique<workspace::HistoryDB>(db_path);
    }

    bool tool_uses_command_pipeline(std::string_view tool_name) const {
        return tool_name == "apply_patch" || tool_name == "revert_patch" ||
               tool_name == "run_tests" ||
               tool_name == "restore_checkpoint" || tool_name == "delete_checkpoint" ||
               tool_name == "git_restore" || tool_name == "git_commit" ||
               tool_name == "git_branch" || tool_name == "git_worktree";
    }

    application::RequestContext request_context() const {
        application::RequestContext request;
        request.username = ws_ctx_.username;
        request.workspace_name = ws_ctx_.workspace_name;
        request.session_id = ws_ctx_.session_id;
        return request;
    }

    application::WorkspaceResolver make_workspace_resolver() const {
        return application::WorkspaceResolver(application::WorkspaceResolverConfig{
            ws_ctx_.tier_paths.global_dir,
            ws_ctx_.workspace_name.empty() ? container::String("default") : ws_ctx_.workspace_name,
            ws_ctx_.project_path});
    }

    Json check_command_permission(std::string_view tool_name,
                                  const Json& arguments) const {
        if (!policy_engine_) {
            return Json{{"success", false},
                        {"error_type", "permission_service_unavailable"},
                        {"message", "permission service unavailable"}};
        }
        auto decision = policy_engine_->evaluate_tool_permission(tool_name, arguments);
        if (decision.allowed()) {
            return Json{{"success", true}, {"policy_effect", "allow"}, {"policy_key", decision.policy_key}};
        }
        return permission::to_json(decision);
    }

    domain::AppResult<void> create_command_checkpoint(const application::CommandDescriptor& command) const {
        if (!checkpoint_service_ || !command.mutates_workspace || command.affected_paths.empty()) {
            return domain::AppResult<void>::success();
        }
        std::vector<std::string> paths;
        for (const auto& path : command.affected_paths) paths.emplace_back(path.c_str());
        auto result = checkpoint_service_->create(paths, "auto checkpoint before " + std::string(command.action.c_str()));
        if (result.ok()) return domain::AppResult<void>::success();
        return domain::AppResult<void>::failure(result.error());
    }

    Json append_command_audit(const container::String& workspace,
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
        audit::AuditStore store(ws_ctx_.tier_paths.user_dir / "audit" / "events.jsonl");
        return store.append(std::move(event));
    }


    Json append_runtime_execution(const container::String&,
                                  const container::String&,
                                  const container::String&,
                                  const Json& execution) const {
        audit::RuntimeExecutionStore store(ws_ctx_.tier_paths.user_dir / "runtime" / "executions.jsonl");
        return store.append(execution);
    }

    application::CommandPipeline make_command_pipeline() const {
        return application::make_command_pipeline(application::CommandGovernanceConfig{
            [this](const container::String&, const container::String&, const container::String&, std::string_view tool_name, const Json& arguments) {
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
                return append_command_audit(workspace, session_id, username, category, action, details);
            },
            [this](const container::String& workspace,
                   const container::String& session_id,
                   const container::String& username,
                   const Json& execution) {
                return append_runtime_execution(workspace, session_id, username, execution);
            }});
    }

    void init_tools() {
        log::debug_fmt("init: tools");
        policy_engine_ = std::make_shared<permission::PolicyEngine>(ws_ctx_);
        patch_service_ = std::make_shared<patch::PatchService>(ws_ctx_);
        auto patch_workspace_resolver = std::make_shared<application::WorkspaceResolver>(make_workspace_resolver());
        patch_use_cases_ = std::make_shared<application::PatchUseCases>(*patch_workspace_resolver, make_command_pipeline());
        patch_workspace_resolver_ = std::move(patch_workspace_resolver);
        git_service_ = std::make_shared<git::GitService>(ws_ctx_);
        checkpoint_service_ = std::make_shared<checkpoint::CheckpointService>(ws_ctx_);
        test_loop_service_ = std::make_shared<test_loop::TestLoopService>(ws_ctx_);
        workspace_index_service_ = std::make_shared<workspace_index::WorkspaceIndexService>(ws_ctx_);
        repo_map_service_ = std::make_shared<repo_map::RepoMapService>(ws_ctx_, git_service_, test_loop_service_, workspace_index_service_);
        code_intel_service_ = std::make_shared<code_intel::CodeIntelService>(ws_ctx_, repo_map_service_);
        diagnostic_context_service_ =
            std::make_shared<diagnostic_context::DiagnosticContextService>(ws_ctx_, code_intel_service_);
        diagnostic_repair_plan_service_ =
            std::make_shared<diagnostic_repair::DiagnosticRepairPlanService>(ws_ctx_, diagnostic_context_service_);
        diagnostic_repair_patch_preview_service_ =
            std::make_shared<diagnostic_repair::DiagnosticRepairPatchPreviewService>(ws_ctx_, diagnostic_repair_plan_service_, patch_service_);
        tools::register_all_tools(tools_, settings_.agent.command_timeout, &skill_loader_, *util_context_);
        auto pipeline = make_command_pipeline();
        auto request = request_context();
        tools::register_patch_tools(tools_, patch_service_, patch_use_cases_, request);
        tools::register_git_tools(tools_, git_service_, pipeline, request, ws_ctx_.project_path);
        tools::register_checkpoint_tools(tools_, checkpoint_service_, pipeline, request, ws_ctx_.project_path);
        tools::register_test_loop_tools(tools_, test_loop_service_, pipeline, request, ws_ctx_.project_path);
        tools::register_repo_map_tools(tools_, repo_map_service_);
        tools::register_code_intel_tools(tools_, code_intel_service_);
        tools::register_diagnostic_context_tools(tools_, diagnostic_context_service_);
        tools::register_diagnostic_repair_tools(tools_, diagnostic_repair_plan_service_, diagnostic_repair_patch_preview_service_);
        tools::register_permission_tools(tools_, policy_engine_);
        tools::register_memory_tools(tools_, memory_store_);
        tools::register_workspace_tools(tools_, ws_manager_);
        tools::register_history_tools(tools_, *history_db_, ws_ctx_);
        tools_.register_tool(
            container::String("update_todo"),
            container::String("Update the session TODO list for non-trivial execution work. Use only when a visible TODO list helps."),
            {
                {container::String("action"), {container::String("string"), container::String("set_items, update_item, or clear"), {container::String("set_items"), container::String("update_item"), container::String("clear")}, true}},
                {container::String("items"), {container::String("array"), container::String("TODO items for set_items; each item has id, title, status, progress, result_summary"), {}, false}},
                {container::String("item"), {container::String("object"), container::String("Single TODO item for update_item"), {}, false}},
            },
            [](const Json&) -> container::String { return container::String("handled by agent session"); });
        // 子 Agent 运行时（延迟初始化，需要 shared_from_this）
    }

    /// 初始化子 Agent 运行时（在 post_init 中调用，需要 shared_from_this）
    void init_sub_agent() {
        log::debug_fmt("init: sub_agent");
        sub_agent_runtime_ = std::make_shared<SubAgentRuntime>(
            shared_from_this(),
            settings_.agent.sub_agent,
            nullptr,  // parent_event_sink 在 ChatRepl 中设置
            container::String(session_id_for_sub_agent()));
        tools::register_sub_agent_tools(tools_, sub_agent_runtime_);
        log::info_fmt("init: sub_agent runtime created, max_parallel={}", settings_.agent.sub_agent.max_parallel);
    }

    /// 获取当前会话 ID（用于子 Agent parent_session_id）
    std::string session_id_for_sub_agent() const {
        return std::string(ws_ctx_.session_id.data(), ws_ctx_.session_id.size());
    }

    void init_skills() {
        log::debug_fmt("init: skills");
        skill_loader_.discover();
        for (auto& def : tools::builtin_skill_definitions()) {
            skill_loader_.add_skill(def);
        }
    }

    void init_mcp() {
        log::debug_fmt("init: MCP");
        if (!settings_.mcp_servers.empty()) {
            auto mcp_ptr = std::shared_ptr<mcp::MCPManager>(
                &mcp_manager_, [](mcp::MCPManager*){});
            mcp_manager_.load_servers(settings_.mcp_servers);
            for (const auto& tool_def : mcp_manager_.all_tool_definitions()) {
                std::string raw_name(tool_def.name);
                std::string mcp_name = "mcp_" + raw_name;
                tools_.register_tool(
                    container::String(mcp_name.c_str()),
                    tool_def.description,
                    tool_def.parameters,
                    [mcp_ptr, raw_name](const Json& args) -> std::string {
                        return mcp_ptr->execute_tool(raw_name, args);
                    });
                log::info_fmt("registered MCP tool: {} -> {}", raw_name, mcp_name);
            }
        }
    }

    void init_workflow() {
        log::debug_fmt("init: workflow");
        template_lib_->register_template(workflow::templates::code_review());
        template_lib_->register_template(workflow::templates::documentation());
        template_lib_->register_template(workflow::templates::refactoring());
        template_lib_->register_template(workflow::templates::test_generation());
        log::info_fmt("registered {} workflow templates", template_lib_->size());
    }

    config::Settings settings_;
    llm::ProviderClient provider_;
    llm::ToolRegistry tools_;
    workspace::WorkspaceContext ws_ctx_;
    std::shared_ptr<permission::PolicyEngine> policy_engine_;
    std::shared_ptr<patch::PatchService> patch_service_;
    std::shared_ptr<application::WorkspaceResolver> patch_workspace_resolver_;
    std::shared_ptr<application::PatchUseCases> patch_use_cases_;
    std::shared_ptr<git::GitService> git_service_;
    std::shared_ptr<checkpoint::CheckpointService> checkpoint_service_;
    std::shared_ptr<test_loop::TestLoopService> test_loop_service_;
    std::shared_ptr<workspace_index::WorkspaceIndexService> workspace_index_service_;
    std::shared_ptr<repo_map::RepoMapService> repo_map_service_;
    std::shared_ptr<code_intel::CodeIntelService> code_intel_service_;
    std::shared_ptr<diagnostic_context::DiagnosticContextService> diagnostic_context_service_;
    std::shared_ptr<diagnostic_repair::DiagnosticRepairPlanService> diagnostic_repair_plan_service_;
    std::shared_ptr<diagnostic_repair::DiagnosticRepairPatchPreviewService> diagnostic_repair_patch_preview_service_;
    skill::SkillLoader skill_loader_;
    std::shared_ptr<memory::MemoryStore> memory_store_;
    std::unique_ptr<memory::ContextBuilder> context_builder_;
    std::unique_ptr<workspace::HistoryDB> history_db_;
    std::shared_ptr<workspace::WorkspaceManager> ws_manager_;
    mcp::MCPManager mcp_manager_;
    std::shared_ptr<base::concurrency::ThreadPool> core_pool_;
    std::shared_ptr<net::IoContext> io_context_;
    std::shared_ptr<net::IoContext> wf_context_;
    std::shared_ptr<net::IoContext> util_context_;
    std::shared_ptr<workflow::WorkflowEngine> workflow_engine_;
    std::shared_ptr<workflow::WorkflowTemplateLibrary> template_lib_;
    std::shared_ptr<SubAgentRuntime> sub_agent_runtime_;
    int max_tool_steps_;
    int max_tool_calls_;
    int max_tool_calls_per_step_;
};

}  // namespace ben_gear::agent

namespace ben_gear {
using SharedResources = agent::SharedResources;
}  // namespace ben_gear
