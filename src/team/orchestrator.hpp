#pragma once

#include "team/types.hpp"
#include "team/context.hpp"
#include "team/agent.hpp"

#include "config/settings.hpp"
#include "llm/provider_client.hpp"
#include "capabilities/tool/registry.hpp"
#include "base/core/event_bus.hpp"

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace ben_gear::team {

/// 团队编排器 — 管理团队生命周期、Agent 通信、工作流调度
///
/// 职责：
/// - 加载/卸载团队定义
/// - 管理 Agent 生命周期（唤醒/休眠）
/// - 按策略调度工作流
/// - 提供只读状态快照（供工具/API 使用）
///
/// 不依赖任何 UI/序列化格式。
class TeamOrchestrator {
public:
    TeamOrchestrator(
        const config::Settings& settings,
        llm::ProviderClient& provider,
        const capabilities::tool::ToolRegistry& tools,
        base::EventBus* event_bus = nullptr);

    ~TeamOrchestrator();

    /// 注册团队（从 .md 文件加载）
    bool register_team(const std::filesystem::path& teams_dir,
                       const std::string& team_id);

    /// 获取已注册的团队 ID 列表
    std::vector<std::string> list_teams() const;

    /// 获取团队定义
    std::optional<TeamDef> get_team(const std::string& team_id) const;

    /// 启动团队工作流
    std::string start(const std::string& team_id,
                      const std::string& objective);

    /// 向团队中的某个 Agent 派发任务
    bool dispatch(const std::string& team_id,
                  const std::string& agent_id,
                  const std::string& task);

    /// 获取团队状态快照
    std::optional<TeamStatus> get_status(const std::string& team_id) const;

    /// 获取团队上下文（黑板内容）
    TeamContext* context(const std::string& team_id);

    /// 让团队休眠
    bool sleep_team(const std::string& team_id);

private:
    struct TeamInstance {
        TeamDef def;
        TeamContext ctx;
        std::unordered_map<std::string, std::unique_ptr<PersistentAgent>> agents;
        bool running = false;
        std::string execution_id;
    };

    // 内部：持有锁时获取 TeamInstance 引用
    // 调用者必须持有 mutex_ 锁
    TeamInstance* unsafe_find(const std::string& team_id);
    const TeamInstance* unsafe_find(const std::string& team_id) const;

    // 内部执行
    std::string do_start(TeamInstance& team, const std::string& objective);
    std::string do_pipeline(TeamInstance& team, const std::string& objective);
    std::string do_sequential(TeamInstance& team, const std::string& objective);
    std::string do_parallel(TeamInstance& team, const std::string& objective);
    agent::SubAgentResult do_run_agent(TeamInstance& team,
                                        const std::string& agent_id,
                                        const std::string& task);

    const config::Settings* settings_;
    llm::ProviderClient* provider_;
    const capabilities::tool::ToolRegistry* tools_;
    base::EventBus* event_bus_;

    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::unique_ptr<TeamInstance>> teams_;
};

} // namespace ben_gear::team
