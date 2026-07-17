#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace ben_gear {
namespace agent::runtime { class Runtime; }
namespace workspace { class Session; }
namespace cli {

class CliApp;

/// dispatch() 返回值：控制 REPL 行为
enum class DispatchResult { Continue, Exit, NewSession };

struct SlashCommandContext {
    agent::runtime::Runtime& agent;
    workspace::Session& session;
    CliApp& cli_app;
    std::function<bool(const std::string&)> confirm_delete;
};

/// 单个命令条目
struct CommandEntry {
    std::string description;                                    // 一行中文描述（用于 /help）
    std::function<DispatchResult(std::string_view)> handler;    // 命令处理函数
};

class SlashCommandDispatcher {
public:
    explicit SlashCommandDispatcher(SlashCommandContext context);

    /// 分发一条斜杠命令
    DispatchResult dispatch(const std::string& line);

    /// 获取已注册命令列表（供 /help 和补全使用）
    const std::unordered_map<std::string, CommandEntry>& commands() const { return commands_; }

private:
    SlashCommandContext context_;
    std::unordered_map<std::string, CommandEntry> commands_;

    // ---- 命令处理函数 ----
    DispatchResult cmd_help(std::string_view args);
    DispatchResult cmd_exec(std::string_view args);
    DispatchResult cmd_file(std::string_view args);
    DispatchResult cmd_export(std::string_view args);
    DispatchResult cmd_plan(std::string_view args);
    DispatchResult cmd_approve(std::string_view args);
    DispatchResult cmd_cancel(std::string_view args);
    DispatchResult cmd_compact(std::string_view args);
    DispatchResult cmd_clear(std::string_view args);
    DispatchResult cmd_model(std::string_view args);
    DispatchResult cmd_sessions(std::string_view args);
    DispatchResult cmd_history(std::string_view args);
    DispatchResult cmd_resume(std::string_view args);
    DispatchResult cmd_new(std::string_view args);
    DispatchResult cmd_search(std::string_view args);
    DispatchResult cmd_config(std::string_view args);
    DispatchResult cmd_tools(std::string_view args);
};

}  // namespace cli
}  // namespace ben_gear
