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
          mcp_manager_(settings_.mcp.read_buffer_size),  // IoContext 在 post_init 中绑定
          core_pool_(std::make_shared<base::concurrency::ThreadPool>(
              base::concurrency::to_thread_pool_config(settings_.thread_pool))),
          io_context_(std::make_shared<net::IoContext>("io")),
          wf_context_(std::make_shared<net::IoContext>("workflow")),
          util_context_(std::make_shared<net::IoContext>("util")),
          // WorkflowEngine 使用 std::async（传 nullptr），避免占用核心线程池
          // 设计意图：工作流任务多为 I/O 密集型（网络请求、工具调用），不应占用核心调度线程
          // 
          // 未来优化：如果需要控制并发度，可以创建独立的 I/O 线程池：
          //   auto io_pool = std::make_shared<ThreadPool>(ThreadPoolConfig{.min_threads=2, .max_threads=4});
          //   workflow_engine_(std::make_shared<workflow::WorkflowEngine>(nullptr, io_pool)),
          workflow_engine_(std::make_shared<workflow::WorkflowEngine>(nullptr, nullptr)),
          template_lib_(std::make_shared<workflow::WorkflowTemplateLibrary>()),
          max_tool_steps_(settings_.agent.max_tool_steps) {
        init();
    }

    // --- 访问器（全部线程安全）---

    const config::Settings& settings() const noexcept { return settings_; }
    const llm::ProviderClient& provider() const noexcept { return provider_; }
    const llm::ToolRegistry& tools() const noexcept { return tools_; }
    
    /// 获取可修改的工具注册表（用于 Session 构造时注册情景工具）
    /// 注意：此方法破坏了 const 访问器的线程安全承诺，仅在单线程初始化阶段使用
    /// 未来优化：移除此方法，改为 Session 构造时传入需要注册的工具列表
    llm::ToolRegistry& tools_mut() noexcept { return tools_; }
    
    const skill::SkillLoader& skill_loader() const noexcept { return skill_loader_; }
    const std::shared_ptr<memory::MemoryStore>& memory_store() const noexcept { return memory_store_; }
    const std::unique_ptr<memory::ContextBuilder>& context_builder() const noexcept { return context_builder_; }
    workspace::HistoryDB& history_db() noexcept { return *history_db_; }
    const std::shared_ptr<workspace::WorkspaceManager>& workspace_manager() const noexcept { return ws_manager_; }
    mcp::MCPManager& mcp_manager() noexcept { return mcp_manager_; }
    const workspace::WorkspaceContext& workspace_context() const noexcept { return ws_ctx_; }
    int max_tool_steps() const noexcept { return max_tool_steps_; }

    /// 核心调度线程池（工具调用+轻量级任务+后续扩展核心业务）
    const std::shared_ptr<base::concurrency::ThreadPool>& core_pool() const noexcept { return core_pool_; }

    /// 共享工作流引擎（Agent 级，多会话通过命名空间隔离）
    const std::shared_ptr<workflow::WorkflowEngine>& workflow_engine() const noexcept { return workflow_engine_; }

    /// I/O 上下文（主 EventLoop，处理 LLM 请求和 HTTP 连接）
    const std::shared_ptr<net::IoContext>& io_context() const noexcept { return io_context_; }

    /// 工作流 I/O 上下文（子 Agent LLM 请求和工作流编排）
    const std::shared_ptr<net::IoContext>& wf_context() const noexcept { return wf_context_; }

    /// 工具 I/O 上下文（临时性任务：技能下载、HTTP工具调用等，不占用核心链路）
    const std::shared_ptr<net::IoContext>& util_context() const noexcept { return util_context_; }

    /// 全局工作流模板库（只读，所有会话共享）
    const std::shared_ptr<workflow::WorkflowTemplateLibrary>& template_lib() const noexcept { return template_lib_; }

    void register_tool(const container::String& name,
                       const container::String& description,
                       const base::container::Vector<std::pair<container::String, llm::ToolParameterSchema>>& parameters,
                       llm::ToolExecutor executor) {
        tools_.register_tool(name, description, parameters, std::move(executor));
    }

    /// 构建 SessionDeps，供 Session 构造使用
    workspace::SessionDeps make_session_deps() const {
        return workspace::SessionDeps{
            .ws_ctx = ws_ctx_,
            .memory_store = memory_store_,
            .context_builder = context_builder_.get(),
            .thread_pool = core_pool_
        };
    }

private:
    void init() {
        init_workspace();
        init_memory();
        init_history();
        init_tools();
        init_skills();
        init_mcp();
        init_workflow();
    }

public:

    /// 构造后调用，注册需要 shared_from_this 的工具（工作流工具）
    /// 注意：必须在 shared_ptr 构造完成后调用，否则 shared_from_this() 会抛异常
    void post_init() {
        // 绑定 SharedResources 到工作流引擎（构造时 resources_ 还不可用）
        workflow_engine_->bind_resources(shared_from_this());
        // 绑定 IoContext 到 MCP（临时性 I/O，不走核心链路）
        mcp_manager_.set_io_context(util_context_.get());
        // 注册 HTTP 工具（使用 util IoContext，不占用核心链路）
        tools::register_http_tools(tools_, *util_context_);
        // 注册工作流工具（需要 SharedResources）
        tools::register_workflow_tools_with_resources(tools_, workflow_engine_, template_lib_);
        log::info_fmt("http + workflow tools registered with SharedResources");
    }

    /// 工作空间管理器
    void init_workspace() {
        log::debug_fmt("init: workspace");
        ws_manager_ = std::make_shared<workspace::WorkspaceManager>(ws_ctx_.tier_paths.user_dir);
    }

    /// 记忆系统：MemoryStore + EpisodeStore + ContextBuilder
    void init_memory() {
        log::debug_fmt("init: memory");
        memory_store_ = std::make_shared<memory::MemoryStore>(ws_ctx_.tier_paths);
        context_builder_ = std::make_unique<memory::ContextBuilder>(*memory_store_, skill_loader_);
        context_builder_->set_project_dir(settings_.workspace);
        if (!settings_.agent.system_prompt.empty()) {
            context_builder_->set_core_prompt(
                std::string(settings_.agent.system_prompt.data(),
                            settings_.agent.system_prompt.size()));
        }
    }

    /// 历史数据库
    void init_history() {
        log::debug_fmt("init: history");
        auto db_path = ws_ctx_.tier_paths.user_dir / "history.db";
        history_db_ = std::make_unique<workspace::HistoryDB>(db_path);
    }

    /// 工具注册：内置（文件/Shell/扩展）+ 记忆 + 工作空间
    /// HTTP/技能工具需要 IoContext，工作流工具需要 SharedResources，在 post_init() 中注册
    void init_tools() {
        log::debug_fmt("init: tools");
        tools::register_all_tools(tools_, settings_.agent.command_timeout, &skill_loader_, *util_context_);
        tools::register_memory_tools(tools_, memory_store_);
        tools::register_workspace_tools(tools_, ws_manager_);
    }

    /// 技能发现与注册
    void init_skills() {
        log::debug_fmt("init: skills");
        skill_loader_.discover();
        for (auto& def : tools::builtin_skill_definitions()) {
            skill_loader_.add_skill(def);
        }
    }

    /// MCP 服务加载与工具注册
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

    /// 工作流引擎 + 模板库初始化
    void init_workflow() {
        log::debug_fmt("init: workflow");
        // 注册内置模板
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
    std::shared_ptr<base::concurrency::ThreadPool> core_pool_;  // 核心调度线程池
    std::shared_ptr<net::IoContext> io_context_;   // 主 I/O 上下文（LLM + HTTP）
    std::shared_ptr<net::IoContext> wf_context_;   // 工作流 I/O 上下文（子 Agent + DAG）
    std::shared_ptr<net::IoContext> util_context_; // 工具 I/O 上下文（临时任务，不占核心）
    std::shared_ptr<workflow::WorkflowEngine> workflow_engine_;   // 共享工作流引擎
    std::shared_ptr<workflow::WorkflowTemplateLibrary> template_lib_;  // 全局模板库
    int max_tool_steps_;
};

}  // namespace ben_gear::agent

namespace ben_gear {
using SharedResources = agent::SharedResources;
}  // namespace ben_gear
