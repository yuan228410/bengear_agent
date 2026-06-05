#pragma once

#include "ben_gear/config/settings.hpp"
#include "ben_gear/llm/provider_client.hpp"
#include "ben_gear/tool/registry.hpp"
#include "ben_gear/tool/types.hpp"
#include "ben_gear/skill/skill.hpp"
#include "ben_gear/memory/store.hpp"
#include "ben_gear/memory/episode.hpp"
#include "ben_gear/memory/context.hpp"
#include "ben_gear/session/history_db.hpp"
#include "ben_gear/role/loader.hpp"
#include "ben_gear/workspace/types.hpp"
#include "ben_gear/workspace/manager.hpp"
#include "ben_gear/mcp/mcp_client.hpp"
#include "ben_gear/tools/skill_tools.hpp"
#include "ben_gear/tools/memory_tools.hpp"
#include "ben_gear/tools/workspace_tools.hpp"
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
class SharedResources {
public:
    explicit SharedResources(config::Settings settings,
                             workspace::WorkspaceContext ws_ctx)
        : settings_(std::move(settings)),
          provider_(settings_),
          tools_(llm::ToolRegistry()),
          ws_ctx_(std::move(ws_ctx)),
          skill_loader_(skill::make_skill_loader(ws_ctx_.tier_paths)),
          mcp_manager_(settings_.mcp.read_buffer_size),
          max_tool_steps_(settings_.agent.max_tool_steps) {
        init();
    }

    // --- 访问器（全部线程安全）---

    const config::Settings& settings() const noexcept { return settings_; }
    const llm::ProviderClient& provider() const noexcept { return provider_; }
    const llm::ToolRegistry& tools() const noexcept { return tools_; }
    const skill::SkillLoader& skill_loader() const noexcept { return skill_loader_; }
    const std::shared_ptr<memory::MemoryStore>& memory_store() const noexcept { return memory_store_; }
    const std::shared_ptr<memory::EpisodeStore>& episode_store() const noexcept { return episode_store_; }
    const std::unique_ptr<memory::ContextBuilder>& context_builder() const noexcept { return context_builder_; }
    session::HistoryDB& history_db() noexcept { return *history_db_; }
    const std::unique_ptr<role::RoleLoader>& role_loader() const noexcept { return role_loader_; }
    const std::shared_ptr<workspace::WorkspaceManager>& workspace_manager() const noexcept { return ws_manager_; }
    mcp::MCPManager& mcp_manager() noexcept { return mcp_manager_; }
    const workspace::WorkspaceContext& workspace_context() const noexcept { return ws_ctx_; }
    int max_tool_steps() const noexcept { return max_tool_steps_; }

    void register_tool(const container::String& name,
                       const container::String& description,
                       const base::container::Vector<std::pair<container::String, llm::ToolParameterSchema>>& parameters,
                       llm::ToolExecutor executor) {
        tools_.register_tool(name, description, parameters, std::move(executor));
    }

private:
    void init() {
        ws_manager_ = std::make_shared<workspace::WorkspaceManager>(ws_ctx_.tier_paths.user_dir);

        memory_store_ = std::make_shared<memory::MemoryStore>(ws_ctx_.tier_paths);
        episode_store_ = std::make_shared<memory::EpisodeStore>();
        context_builder_ = std::make_unique<memory::ContextBuilder>(*memory_store_, skill_loader_);
        context_builder_->set_project_dir(settings_.workspace);
        if (!settings_.agent.system_prompt.empty()) {
            context_builder_->set_core_prompt(
                std::string(settings_.agent.system_prompt.data(),
                            settings_.agent.system_prompt.size()));
        }

        auto db_path = ws_ctx_.tier_paths.user_dir / "history.db";
        history_db_ = std::make_unique<session::HistoryDB>(db_path);

        tools::register_all_tools(tools_, settings_.agent.command_timeout, &skill_loader_);
        tools::register_memory_tools(tools_, memory_store_, episode_store_,
            ws_ctx_.tier_paths.workspace_dir / "memory_data" / "sessions");
        tools::register_workspace_tools(tools_, ws_manager_);

        skill_loader_.discover();
        for (auto& def : tools::builtin_skill_definitions()) {
            skill_loader_.add_skill(def);
        }

        role_loader_ = std::make_unique<role::RoleLoader>(
            ws_ctx_.tier_paths.global_dir,
            ws_ctx_.tier_paths.user_dir,
            ws_ctx_.tier_paths.workspace_dir);
        role_loader_->discover();

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

    config::Settings settings_;
    llm::ProviderClient provider_;
    llm::ToolRegistry tools_;
    workspace::WorkspaceContext ws_ctx_;
    skill::SkillLoader skill_loader_;
    std::shared_ptr<memory::MemoryStore> memory_store_;
    std::shared_ptr<memory::EpisodeStore> episode_store_;
    std::unique_ptr<memory::ContextBuilder> context_builder_;
    std::unique_ptr<session::HistoryDB> history_db_;
    std::unique_ptr<role::RoleLoader> role_loader_;
    std::shared_ptr<workspace::WorkspaceManager> ws_manager_;
    mcp::MCPManager mcp_manager_;
    int max_tool_steps_;
};

}  // namespace ben_gear::agent

namespace ben_gear {
using SharedResources = agent::SharedResources;
}  // namespace ben_gear
