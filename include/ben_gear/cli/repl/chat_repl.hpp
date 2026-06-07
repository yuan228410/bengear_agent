#pragma once

#include "ben_gear/cli/repl/line_editor.hpp"
#include "ben_gear/cli/repl/completer.hpp"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ben_gear {

namespace agent { class Agent; }
namespace workspace { class Session; }
namespace cli { class CliApp; }

/// 交互式聊天 REPL
///
/// 高度封装：组合 LineEditor + Agent + CliApp
/// 外部只需构造并调用 run()
class ChatRepl {
public:
    struct Config {
        std::string prompt;
        bool enable_history;
    };

    ChatRepl(agent::Agent& agent, workspace::Session& session,
             std::unique_ptr<cli::CliApp> cli_app,
             Config config = {});

    int run();

private:
    agent::Agent& agent_;
    workspace::Session& session_;
    std::unique_ptr<cli::CliApp> cli_app_;
    Config config_;
    cli::LineEditor editor_;

    void register_commands();
    bool handle_command(const std::string& line);
    int interrupt_count_ = 0;
    bool send_message(const std::string& prompt);
};

}  // namespace ben_gear
