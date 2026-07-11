#pragma once

#include <functional>
#include <string>

namespace ben_gear {
namespace agent { class Agent; }
namespace workspace { class Session; }
namespace cli {

class CliApp;

struct SlashCommandContext {
    agent::Agent& agent;
    workspace::Session& session;
    CliApp& cli_app;
    std::function<bool(const std::string&)> confirm_delete;
};

class SlashCommandDispatcher {
public:
    explicit SlashCommandDispatcher(SlashCommandContext context);

    /// Dispatches one slash command. Returns false when the REPL should exit.
    bool dispatch(const std::string& line);

private:
    SlashCommandContext context_;
};

}  // namespace cli
}  // namespace ben_gear
