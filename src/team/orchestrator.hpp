#pragma once

#include "team/types.hpp"
#include "team/context.hpp"
#include "team/agent.hpp"

#include "config/settings.hpp"
#include "llm/provider_client.hpp"
#include "capabilities/tool/registry.hpp"
#include "base/core/event_bus.hpp"
#include "workspace/history_db.hpp"

#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace ben_gear::team {

class TeamOrchestrator {
public:
    TeamOrchestrator(
        const config::Settings& settings,
        llm::ProviderClient& provider,
        const capabilities::tool::ToolRegistry& tools,
        base::EventBus* event_bus = nullptr,
        workspace::HistoryDB* history_db = nullptr);
    ~TeamOrchestrator();

    bool register_team(const std::filesystem::path& teams_dir,
                       const std::string& team_id);
    std::vector<std::string> list_teams() const;
    std::optional<TeamDef> get_team(const std::string& team_id) const;
    std::string start(const std::string& team_id,
                      const std::string& objective);
    /// 按计划项顺序执行：每个 item 分派给指定 Member（或轮转），结果发布到黑板
    std::string start_with_plan(const std::string& team_id,
                                const std::vector<PlanTaskItem>& items);
    bool dispatch(const std::string& team_id,
                  const std::string& agent_id,
                  const std::string& task);
    std::optional<TeamStatus> get_status(const std::string& team_id) const;
    TeamContext* context(const std::string& team_id);

    /// 安全发送团队消息（内部持锁访问 TeamContext，消除裸指针风险）
    bool send_team_message(const std::string& team_id,
                           const std::string& from,
                           const std::string& to,
                           const std::string& subject,
                           const std::string& body);
    /// 安全读取团队成员收件箱并返回 JSON
    std::string read_team_messages_json(const std::string& team_id,
                                        const std::string& member);
    /// 安全向全体成员广播消息，返回成功发送数量
    int broadcast_team_message(const std::string& team_id,
                                const std::string& subject,
                                const std::string& body);

    /// 安全获取团队快照 JSON（用于 run_team 工具结果展示）
    std::string team_snapshot_json(const std::string& team_id) const;
    /// 安全读取团队黑板指定 key 的值
    std::string read_team_artifact(const std::string& team_id,
                                   const std::string& key) const;

    bool sleep_team(const std::string& team_id);

    /// 查询团队执行历史（从 SQLite 加载）
    std::vector<Json> list_history(const std::string& team_id, int limit = 20) const;

private:
    struct TeamInstance {
        TeamDef def;
        TeamContext ctx;
        std::unordered_map<std::string, std::unique_ptr<PersistentAgent>> agents;
        bool running = false;
        std::string execution_id;
    };

    /// 持锁查找（调用者已持有锁）
    std::shared_ptr<TeamInstance> unsafe_find(const std::string& team_id);
    std::shared_ptr<const TeamInstance> unsafe_find(const std::string& team_id) const;

    /// 安全查找并执行操作（内部持锁，操作完成后释放）
    template <typename Func>
    auto with_team(const std::string& team_id, Func&& func)
        -> std::optional<decltype(func(std::declval<TeamInstance&>()))> {
        std::shared_lock lock(mutex_);
        auto team = unsafe_find(team_id);
        if (!team) return std::nullopt;
        return func(*team);
    }

    std::string do_start(TeamInstance& team, const std::string& objective);
    std::string do_pipeline(TeamInstance& team, const std::string& objective);
    std::string do_sequential(TeamInstance& team, const std::string& objective);
    std::string do_parallel(TeamInstance& team, const std::string& objective);
    bool ensure_agent(TeamInstance& team, const std::string& agent_id);
    agent::SubAgentResult do_run_agent(TeamInstance& team,
                                        const std::string& agent_id,
                                        const std::string& task);

    /// 持久化执行记录到 SQLite
    void persist_execution(const TeamInstance& team,
                           const std::string& objective);

    const config::Settings* settings_;
    llm::ProviderClient* provider_;
    const capabilities::tool::ToolRegistry* tools_;
    base::EventBus* event_bus_;
    workspace::HistoryDB* history_db_;

    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<TeamInstance>> teams_;
};

} // namespace ben_gear::team
