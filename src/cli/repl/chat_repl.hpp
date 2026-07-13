#pragma once

#include "cli/repl/line_editor.hpp"
#include "cli/repl/completer.hpp"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ben_gear {

namespace agent::runtime { class Runtime; }
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
        bool show_banner;
        bool is_resumed_session;          // 是否恢复了历史会话
    };

    ChatRepl(agent::runtime::Runtime& agent, workspace::Session& session,
             std::unique_ptr<cli::CliApp> cli_app,
             Config config = {});

    int run();

private:
    agent::runtime::Runtime& agent_;
    workspace::Session& session_;
    std::unique_ptr<cli::CliApp> cli_app_;
    Config config_;
    cli::LineEditor editor_;

    void register_commands();
    bool handle_command(const std::string& line);
    int interrupt_count_ = 0;
    size_t last_persisted_count_ = 0;  // 上次持久化后的消息数
    bool send_message(const std::string& prompt);
};

}  // namespace ben_gear
