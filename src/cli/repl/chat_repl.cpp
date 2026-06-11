#include "ben_gear/cli/repl/chat_repl.hpp"
#include "ben_gear/cli/render/cli_app.hpp"
#include "ben_gear/cli/render/terminal.hpp"
#include "ben_gear/cli/render/theme.hpp"
#include "ben_gear/cli/render/markdown.hpp"
#include "ben_gear/cli/render/highlight.hpp"

#include "ben_gear/agent/agent.hpp"
#include "ben_gear/agent/plan_manager.hpp"
#include "ben_gear/workspace/session.hpp"
#include "ben_gear/workspace/history_exporter.hpp"
#include "ben_gear/base/net/cancel.hpp"
#include "ben_gear/base/net/event_loop.hpp"
#include "ben_gear/base/utils/string_utils.hpp"
#include "ben_gear/base/log/logger.hpp"
#include "ben_gear/config/settings.hpp"

#include <iostream>
#include <csignal>
#include <ctime>

namespace ben_gear {

using namespace cli;
using agent::Agent;
using workspace::Session;


/// 打印时间戳，如 14:32:05 ❯
static void print_timestamp() {
    auto cap = cli::TerminalCapabilities::detect();
    auto now = std::time(nullptr);
    auto* tm = std::localtime(&now);
    char buf[10];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", tm);
    auto theme = cli::Theme::default_dark();
    auto ts = ansi::colorize(std::string(buf), theme.system_info, StyleFlag::dim, cap);
    std::cout << ts.c_str();
    auto arrow = ansi::colorize(
        cap.unicode ? " \xe2\x9d\xaf" : " >",
        theme.user_prompt, StyleFlag::none, cap);
    std::cout << arrow.c_str();
}

/// ASCII Art banner
static void print_banner(const Agent& agent) {
    auto cap = cli::TerminalCapabilities::detect();
    if (!cap.is_tty) return;

    auto& settings = agent.settings();
    auto theme = cli::Theme::default_dark();

    auto ben_color   = theme.assistant_heading_h2;
    auto gear_color  = theme.assistant_heading_h1;
    auto agent_color = theme.hl_function;
    auto dim_color   = theme.system_info;

    static constexpr int kBenEnd   = 20;
    static constexpr int kGearEnd  = 46;

    static const char* kLines[] = {
        "    ____             ______                   ___                    __ ",
        "   / __ )___  ____  / ____/__  ____ ______   /   | ____ ____  ____  / /_",
        "  / __  / _ \\/ __ \\/ / __/ _ \\/ __ `/ ___/  / /| |/ __ `/ _ \\/ __ \\/ __/",
        " / /_/ /  __/ / / / /_/ /  __/ /_/ / /     / ___ / /_/ /  __/ / / / /_  ",
        "/_____/\\___/_/ /_/\\____/\\___/\\__,_/_/     /_/  |_\\__, /\\___/_/ /_/\\__/  ",
    };

    if (!cap.unicode) {
        auto ben   = ansi::colorize("Ben",   ben_color,   StyleFlag::bold, cap);
        auto gear  = ansi::colorize("Gear",  gear_color,  StyleFlag::bold, cap);
        auto ag    = ansi::colorize(" Agent", agent_color, StyleFlag::bold, cap);
        std::cout << " " << ben.c_str() << gear.c_str() << ag.c_str() << "\n";
    } else {
        for (const auto* line : kLines) {
            auto len = static_cast<int>(std::strlen(line));
            auto ben_len   = std::min(kBenEnd,   len);
            auto gear_len  = std::min(kGearEnd,  len);

            std::string_view sv(line, len);
            auto ben_part   = ansi::colorize(sv.substr(0, ben_len),
                                             ben_color, StyleFlag::none, cap);
            auto gear_part  = ansi::colorize(sv.substr(ben_len, gear_len - ben_len),
                                             gear_color, StyleFlag::none, cap);
            auto agent_part = gear_len < len
                ? ansi::colorize(sv.substr(gear_len), agent_color, StyleFlag::none, cap)
                : container::String();

            std::cout << ben_part.c_str() << gear_part.c_str()
                      << agent_part.c_str() << "\n";
        }
    }

    auto provider_str = std::string(provider_name(settings.provider).c_str());
    auto model_str = std::string(settings.model.c_str());
    std::string info_line = provider_str + " / " + model_str + "  v0.1.0";
    auto info_colored = ansi::colorize(info_line, dim_color, StyleFlag::dim, cap);
    std::cout << " " << info_colored.c_str() << "\n";
}


ChatRepl::ChatRepl(agent::Agent& agent, workspace::Session& session,
                   std::unique_ptr<CliApp> cli_app,
                   Config config)
    : agent_(agent), session_(session), cli_app_(std::move(cli_app)),
      config_(std::move(config)),
      editor_(LineEditor::Config{config_.prompt, {}, config_.enable_history}) {}

int ChatRepl::run() {
    if (config_.show_banner) {
        print_banner(agent_);
    }

    // 注册补全器
    auto completer = std::make_unique<SlashCompleter>(std::vector<SlashCompleter::Command>{
        {"exit", "退出", false},
        {"quit", "退出", false},
        {"help", "显示帮助", false},
        {"new", "创建新会话", false},
        {"sessions", "列出历史会话", false},
        {"history", "显示历史消息", true},
        {"resume", "恢复历史会话", true},
        {"plan", "计划模式（探索）", true},
        {"compact", "手动上下文压缩", false},
        {"clear", "清屏", false},
        {"model", "显示当前模型", true},
        {"search", "搜索历史消息", true},
        {"export", "导出会话为 Markdown", true},
    });

    // /plan 二级子命令补全
    completer->set_sub_provider([](std::string_view cmd) -> std::vector<SlashCompleter::SubCommand> {
        if (cmd == "plan") {
            return {
                {"off", "退出计划模式"},
            };
        }
        return {};
    });

    editor_.set_completer(std::move(completer));

    for (;;) {
        // 根据计划模式动态更新提示符
        auto& pm = agent_.plan_manager();
        if (pm.in_plan_mode()) {
            // ❯ 🔒
            auto cap = TerminalCapabilities::detect();
            if (cap.unicode) {
                editor_.set_prompt(config_.prompt + " \xf0\x9f\x94\x92 "); // 🔒
            } else {
                editor_.set_prompt(config_.prompt + " [plan] ");
            }
        } else {
            editor_.set_prompt(config_.prompt);
        }

        auto line = editor_.read_line();

        if (line.empty()) continue;

        if (line == std::string(LineEditor::kInterrupted)) {
            ++interrupt_count_;
            if (interrupt_count_ >= 2) {
                std::cout << "\n";
                return 0;
            }
            continue;
        }
        interrupt_count_ = 0;

        if (line[0] == '/') {
            if (handle_command(line)) continue;
            return 0;
        }

        send_message(line);
    }
}

bool ChatRepl::handle_command(const std::string& line) {
    // strip 前后空格，避免补全带入的尾部空格导致匹配失败
    auto trimmed = line;
    while (!trimmed.empty() && trimmed.front() == ' ') trimmed.erase(0, 1);
    while (!trimmed.empty() && trimmed.back() == ' ') trimmed.pop_back();

    auto space_pos = trimmed.find(' ');
    auto cmd = (space_pos == std::string::npos) ? trimmed : trimmed.substr(0, space_pos);
    auto args = (space_pos == std::string::npos) ? std::string{} : trimmed.substr(space_pos + 1);
    while (!args.empty() && args.front() == ' ') args.erase(0, 1);

    // /exit 和 /quit 返回 false 让外层退出
    if (cmd == "/exit" || cmd == "/quit") return false;

    if (cmd == "/help" || cmd == "/h") {
        std::cout
            << "Commands:\n"
            << " /exit        - 退出\n"
            << " /help        - 显示帮助\n"
            << " /new         - 创建新会话\n"
            << " /sessions    - 列出历史会话\n"
            << " /history [n] - 显示最近 n 条历史消息（默认 20）\n"
            << " /resume <id> - 恢复历史会话\n"
            << " /plan        - 进入计划模式（read-only 探索）\n"
            << " /plan off    - 退出计划模式\n"
            << " /compact     - 手动上下文压缩\n"
            << " /clear       - 清屏\n"
            << " /model       - 显示当前模型\n"
            << " /search <kw> - 搜索历史消息\n"
            << " /export      - 导出会话为 Markdown\n";

        return true;
    }

    if (cmd == "/export") {
        auto& ws_ctx = agent_.resources()->workspace_context();
        const auto& ws_name = ws_ctx.workspace_name.empty() ? container::String("default") : ws_ctx.workspace_name;

        workspace::ExportOptions opts;
        std::string filename;
        auto arg_str = args;
        while (!arg_str.empty()) {
            auto sp = arg_str.find(' ');
            auto token = (sp == std::string::npos) ? arg_str : arg_str.substr(0, sp);
            arg_str = (sp == std::string::npos) ? std::string{} : arg_str.substr(sp + 1);
            if (token == "--no-tool") { opts.include_tool_calls = false; }
            else if (token == "--no-thinking") { opts.include_thinking = false; }
            else if (token == "--with-result") { opts.include_tool_results = true; }
            else if (!token.empty() && token[0] != '-') { filename = token; }
        }

        if (filename.empty()) {
            auto now = std::time(nullptr);
            char buf[64];
            std::strftime(buf, sizeof(buf), "history_%Y%m%d_%H%M%S.md", std::localtime(&now));
            filename = buf;
        }

        bool ok = workspace::HistoryExporter::export_session_to_file(
            agent_.history_db(), ws_name, session_.session_id(), filename, opts);
        if (ok) {
            std::cout << "Exported to: " << filename << "\n";
        } else {
            std::cerr << "Export failed.\n";
        }
        return true;
    }

    if (cmd == "/plan") {
        auto& pm = agent_.plan_manager();
        auto& callbacks = cli_app_->callbacks();

        // 解析子命令
        auto space_pos = args.find(' ');
        auto subcmd = (space_pos == std::string::npos) ? args : args.substr(0, space_pos);

        if (subcmd == "off") {
            if (pm.in_plan_mode()) {
                pm.exit_plan_mode();
                callbacks.on_mode_changed(PlanManager::Mode::normal);
                log::info_fmt("plan mode exited");
            } else {
                std::cout << "Not in plan mode.\n";
            }
        } else if (pm.in_plan_mode()) {
            // /plan（无子命令）— 已在计划模式
            std::cout << "Already in plan mode. Use /plan off to exit." << std::endl;
        } else {
            // /plan（无子命令）— 进入计划模式
            pm.enter_plan_mode();
            callbacks.on_mode_changed(PlanManager::Mode::planning);
            log::info_fmt("plan mode entered");
        }
        return true;
    }

    if (cmd == "/compact") {
        log::info_fmt("manual compact triggered");
        auto& io_loop = agent_.resources()->io_context()->loop();
        auto before = session_.history().size();
        session_.maybe_compact(io_loop, agent_.resources()->provider(), agent_.resources()->tools());
        auto after = session_.history().size();
        std::cout << "Compacted: " << before << " -> " << after << " messages\n";
        return true;
    }

    if (cmd == "/clear") {
        fwrite("\033[2J\033[H", 7, 1, stdout);
        fflush(stdout);
        return true;
    }

    if (cmd == "/model") {
        auto& settings = agent_.settings();
        std::cout << "Model: " << std::string(settings.model.data(), settings.model.size()) << "\n";
        std::cout << "Provider: " << provider_name(settings.provider) << "\n";
        return true;
    }


    if (cmd == "/sessions") {
        auto& ws_ctx = agent_.resources()->workspace_context();
        const auto& ws_name = ws_ctx.workspace_name.empty() ? container::String("default") : ws_ctx.workspace_name;
        auto sessions = agent_.history_db().list_sessions(ws_name);
        if (sessions.empty()) {
            std::cout << "No sessions found.\n";
        } else {
            std::cout << "Sessions (" << sessions.size() << "):\n";
            for (const auto& s : sessions) {
                auto sid = s.value("session_id", "");
                auto updated = s.value("updated_at", "");
                auto count = s.value("msg_count", 0);
                auto current_sid = std::string(session_.session_id().data(), session_.session_id().size());
                std::cout << "  " << sid;
                if (sid == current_sid) std::cout << " *";
                std::cout << "  msgs=" << count << "  " << updated << "\n";
            }
        }
        return true;
    }

    if (cmd == "/history") {
        int n = 20;
        if (!args.empty()) {
            try { n = std::stoi(args); } catch (...) { n = 20; }
            if (n <= 0) n = 20;
        }
        auto& ws_ctx = agent_.resources()->workspace_context();
        const auto& ws_name = ws_ctx.workspace_name.empty() ? container::String("default") : ws_ctx.workspace_name;
        auto messages = agent_.history_db().load_session(ws_name, session_.session_id());
        if (messages.empty()) {
            std::cout << "No history messages.\n";
            return true;
        }
        int start = static_cast<int>(messages.size()) - n;
        if (start < 0) start = 0;
        auto theme = cli::Theme::default_dark();
        auto cap = cli::TerminalCapabilities::detect();
        for (int i = start; i < static_cast<int>(messages.size()); ++i) {
            auto& msg = messages[i];
            auto role = msg.value("role", "");
            auto content = msg.value("content", "");
            auto ts = msg.value("ts", "");
            if (ts.size() >= 19) ts = "[" + ts.substr(11, 8) + "]";
            if (content.size() > 120) content = content.substr(0, 120) + "...";
            auto ts_colored = ansi::colorize(ts, theme.system_info, StyleFlag::dim, cap);
            if (role == "user") {
                std::cout << ts_colored.c_str() << " " << content << "\n";
            } else if (role == "assistant") {
                auto lbl = ansi::colorize(">> ", theme.assistant_heading_h2, StyleFlag::none, cap);
                std::cout << ts_colored.c_str() << lbl.c_str() << content << "\n";
            } else if (role == "thinking") {
                auto lbl = ansi::colorize("?  ", theme.thinking_label, StyleFlag::none, cap);
                std::cout << ts_colored.c_str() << lbl.c_str() << content << "\n";
            } else if (role == "tool" || role == "tool_call") {
                auto tool_name = msg.value("tool_name", "");
                auto name_str = tool_name.empty() ? role : tool_name;
                auto lbl = ansi::colorize(name_str + " ", theme.tool_name, StyleFlag::none, cap);
                auto display = content.size() > 120 ? content.substr(0, 120) + "..." : content;
                std::cout << ts_colored.c_str() << " " << lbl.c_str() << display << "\n";
            }
        }
        return true;
    }

    if (cmd == "/resume") {
        if (args.empty()) {
            std::cerr << "Usage: /resume <session_id>\n";
            return true;
        }
        log::info_fmt("session resume requested: id={}", args);
        std::cout << "Session resume requested: " << args << "\n";
        return true;
    }

    if (cmd == "/new") {
        log::info_fmt("new session requested");
        std::cout << "New session requested\n";
        return true;
    }

    if (cmd == "/search") {
        if (args.empty()) {
            std::cerr << "Usage: /search <keyword>\n";
            return true;
        }
        auto& ws_ctx = agent_.resources()->workspace_context();
        const auto& ws_name = ws_ctx.workspace_name.empty() ? container::String("default") : ws_ctx.workspace_name;
        auto results = agent_.history_db().search(container::String(args.data(), args.size()), ws_name, 20);
        if (results.empty()) {
            std::cout << "No results found.\n";
        } else {
            for (const auto& r : results) {
                auto role = r.contains("role") ? r["role"].get<std::string>() : "?";
                auto content_str = r.contains("content") ? r["content"].get<std::string>() : "";
                if (content_str.size() > 120) content_str = content_str.substr(0, 120) + "...";
                std::cout << "[" << role << "] " << content_str << "\n";
            }
            std::cout << "--- " << results.size() << " results ---\n";
        }
        return true;
    }

    std::cerr << "Unknown command: " << cmd << "\n";
    return true;
}

bool ChatRepl::send_message(const std::string& prompt) {
    print_timestamp();
    std::cout << " " << prompt << "\n";

    auto& io_loop = agent_.resources()->io_context()->loop();
    auto& callbacks = cli_app_->callbacks();

    log::info_fmt("chat request received stream={}", agent_.settings().stream ? "true" : "false");

    net::CancellationToken cancel;
    editor_.suspend_raw_mode();

    static std::shared_ptr<net::CancellationToken> g_cancel_ptr;
    g_cancel_ptr = std::make_shared<net::CancellationToken>(cancel);

    auto prev_handler = ::signal(SIGINT, [](int) {
        if (g_cancel_ptr) {
            g_cancel_ptr->cancel();
            log::info_fmt("SIGINT received, request cancelled");
        }
    });

    try {
        cli_app_->response_start();
        auto prompt_str = container::String(prompt.data(), prompt.size());
        auto result = net::sync_wait(io_loop,
            agent_.run_session_async(io_loop, session_, std::move(prompt_str), callbacks, cancel));
        cli_app_->response_end();
        if (result.status < 200 || result.status >= 300) {
            log::error_fmt("request failed status={}", result.status);
            std::cerr << "request failed with http status " << result.status << "\n" << result.raw << '\n';
        }
        std::cout << "\n";
    } catch (const net::OperationCancelled&) {
        std::cerr << "\n[cancelled]\n";
    } catch (const std::exception& e) {
        log::error_fmt("chat error: {}", e.what());
        std::cerr << "error: " << e.what() << "\n";
    }

    ::signal(SIGINT, prev_handler);
    g_cancel_ptr.reset();
    return true;
}

}  // namespace ben_gear
