#pragma once

#include "ben_gear/config/settings.hpp"
#include "ben_gear/llm/provider_client.hpp"
#include "ben_gear/tool/registry.hpp"
#include "ben_gear/tool/types.hpp"
#include "ben_gear/skill/skill.hpp"
#include "ben_gear/memory/store.hpp"

#include "ben_gear/memory/context.hpp"
#include "ben_gear/workspace/history_db.hpp"
#include "ben_gear/workspace/types.hpp"
#include "ben_gear/workspace/manager.hpp"
#include "ben_gear/mcp/mcp_client.hpp"
#include "ben_gear/tools/skill_tools.hpp"
#include "ben_gear/tools/memory_tools.hpp"
#include "ben_gear/tools/workspace_tools.hpp"
#include "ben_gear/tools/history_tools.hpp"
#include "ben_gear/tools/sub_agent_tools.hpp"
#include "ben_gear/workflow/workflow_engine.hpp"
#include "ben_gear/workflow/workflow_templates.hpp"
#include "ben_gear/base/concurrency/thread_pool.hpp"
#include "ben_gear/base/net/io_context.hpp"
#include "ben_gear/base/log/logger.hpp"

#include <memory>

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
class SharedResources : public std::enable_shared_from_this<SharedResources> {
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

        context_builder_ = std::make_unique<memory::ContextBuilder>(*memory_store_, skill_loader_);
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

    void init_tools() {
        log::debug_fmt("init: tools");
        tools::register_all_tools(tools_, settings_.agent.command_timeout, &skill_loader_, *util_context_);
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
            nullptr,  // parent_callbacks 在 ChatRepl 中设置
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
