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

class TeamOrchestrator {
public:
    TeamOrchestrator(
        const config::Settings& settings,
        llm::ProviderClient& provider,
        const capabilities::tool::ToolRegistry& tools,
        base::EventBus* event_bus = nullptr);
    ~TeamOrchestrator();

    bool register_team(const std::filesystem::path& teams_dir,
                       const std::string& team_id);
    std::vector<std::string> list_teams() const;
    std::optional<TeamDef> get_team(const std::string& team_id) const;
    std::string start(const std::string& team_id,
                      const std::string& objective);
    bool dispatch(const std::string& team_id,
                  const std::string& agent_id,
                  const std::string& task);
    std::optional<TeamStatus> get_status(const std::string& team_id) const;
    TeamContext* context(const std::string& team_id);
    bool sleep_team(const std::string& team_id);

private:
    struct TeamInstance {
        TeamDef def;
        TeamContext ctx;
        std::unordered_map<std::string, std::unique_ptr<PersistentAgent>> agents;
        bool running = false;
        std::string execution_id;
    };

    TeamInstance* unsafe_find(const std::string& team_id);
    const TeamInstance* unsafe_find(const std::string& team_id) const;

    std::string do_start(TeamInstance& team, const std::string& objective);
    std::string do_pipeline(TeamInstance& team, const std::string& objective);
    std::string do_sequential(TeamInstance& team, const std::string& objective);
    std::string do_parallel(TeamInstance& team, const std::string& objective);
    bool ensure_agent(TeamInstance& team, const std::string& agent_id);
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
