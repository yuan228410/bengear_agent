#include "ben_gear/ben_gear.hpp"

#include <filesystem>
#include <iostream>

int main() {
    auto settings = ben_gear::load_config(std::filesystem::current_path());
    if (settings.api_key.empty()) {
        std::cout << "Set BEN_GEAR_API_KEY before running this example.\n";
        return 0;
    }

    namespace ws = ben_gear::workspace;
    auto root = ben_gear::support::data_directory();
    auto username = settings.username.empty() ? ben_gear::base::container::String("default") : settings.username;
    auto ws_name = settings.workspace_name.empty() ? ben_gear::base::container::String("default") : settings.workspace_name;

    ws::TierPaths tier_paths{
        root,
        root / "users" / std::string(username.data(), username.size()),
        root / "users" / std::string(username.data(), username.size())
             / "workspaces" / std::string(ws_name.data(), ws_name.size())
    };

    ws::WorkspaceContext ws_ctx{
        std::move(tier_paths),
        ws_name,
        username,
        settings.session_id
    };

    ben_gear::Agent agent(std::move(settings), ws_ctx);

    // 创建临时 Session
    auto session = ws::Session(
        ws::SessionConfig{{}, agent.settings().context_length},
        agent.resources()->make_session_deps());

    ben_gear::net::EventLoop loop;
    auto prompt = ben_gear::base::container::String("用一句话介绍 BenGear");
    auto result = loop.run(agent.run_session_async(loop, session, std::move(prompt),
        ben_gear::NullAgentCallbacks()));
    std::cout << result.text << '\n';
}
