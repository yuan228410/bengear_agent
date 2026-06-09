#include "ben_gear/cli/repl/chat_repl.hpp"
#include "ben_gear/cli/render/cli_app.hpp"
#include "ben_gear/cli/render/terminal.hpp"
#include "ben_gear/cli/render/theme.hpp"

#include "ben_gear/agent/agent.hpp"
#include "ben_gear/workspace/session.hpp"
#include "ben_gear/base/net/cancel.hpp"
#include "ben_gear/base/net/event_loop.hpp"
#include "ben_gear/base/utils/string_utils.hpp"
#include "ben_gear/base/log/logger.hpp"
#include "ben_gear/config/settings.hpp"

#include <iostream>
#include <csignal>

namespace ben_gear {

using namespace cli;
using agent::Agent;
using workspace::Session;

/// ASCII Art banner：slant 字体，三段着色 Ben(cyan) / Gear(pink) / Agent(green)
/// 非 unicode 终端使用简洁文本 fallback
static void print_banner(const Agent& agent) {
    auto cap = cli::TerminalCapabilities::detect();
    if (!cap.is_tty) return;

    auto& settings = agent.settings();
    auto theme = cli::Theme::default_dark();

    auto ben_color   = theme.assistant_heading_h2;   // cyan
    auto gear_color  = theme.assistant_heading_h1;   // pink
    auto agent_color = theme.hl_function;            // green
    auto dim_color   = theme.system_info;

    // 三段着色边界（列号）
    static constexpr int kBenEnd   = 20;  // Ben 段: [0, 20)
    static constexpr int kGearEnd  = 46;  // Gear 段: [20, 46)
                                       // Agent 段: [46, ...)

    // slant 字体 ASCII Art
    static const char* kLines[] = {
        "    ____             ______                   ___                    __ ",
        "   / __ )___  ____  / ____/__  ____ ______   /   | ____ ____  ____  / /_",
        "  / __  / _ \\/ __ \\/ / __/ _ \\/ __ `/ ___/  / /| |/ __ `/ _ \\/ __ \\/ __/",
        " / /_/ /  __/ / / / /_/ /  __/ /_/ / /     / ___ / /_/ /  __/ / / / /_  ",
        "/_____/\\___/_/ /_/\\____/\\___/\\__,_/_/     /_/  |_\\__, /\\___/_/ /_/\\__/  ",
        "                                                /____/                   ",
    };

    // 非 unicode 终端 fallback
    if (!cap.unicode) {
        auto ben   = ansi::colorize("Ben",   ben_color,   StyleFlag::bold, cap);
        auto gear  = ansi::colorize("Gear",  gear_color,  StyleFlag::bold, cap);
        auto ag    = ansi::colorize(" Agent", agent_color, StyleFlag::bold, cap);
        std::cout << " " << ben.c_str() << gear.c_str() << ag.c_str() << "\n";
    } else {
        // 逐行渲染，三段着色
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

    // 信息行：provider / model / version
    auto provider_str = std::string(provider_name(settings.provider).c_str());
    auto model_str = std::string(settings.model.c_str());
    std::string info_line = provider_str + " / " + model_str + "  v0.1.0";
    auto info_colored = ansi::colorize(info_line, dim_color, StyleFlag::dim, cap);
    std::cout << " " << info_colored.c_str() << "\n";
    std::cout << "\n";
}


ChatRepl::ChatRepl(agent::Agent& agent, workspace::Session& session,
                   std::unique_ptr<CliApp> cli_app,
                   Config config)
    : agent_(agent), session_(session), cli_app_(std::move(cli_app)),
      config_(std::move(config)),
      editor_(cli::LineEditor::Config{config_.prompt, cli::HistoryStore::default_path(), config_.enable_history, true}) {
    register_commands();
}

int ChatRepl::run() {
    if (config_.show_banner) print_banner(agent_);

    // 补全器在构造时一次性创建
    auto completer = std::make_unique<SlashCompleter>(
        std::vector<SlashCompleter::Command>{
            {"exit",    "退出",          false},
            {"quit",    "退出",          false},
            {"help",    "显示帮助",      false},
            {"new",     "创建新会话",    false},
            {"sessions","列出历史会话",  false},
            {"resume",  "恢复历史会话",  true},
            {"compact", "手动上下文压缩", false},
            {"clear",   "清屏",          false},
            {"model",   "显示当前模型",  true},
        });

    completer->set_sub_provider([this](std::string_view cmd) -> std::vector<container::String> {
        if (cmd == "resume") {
            auto& ws_ctx = agent_.resources()->workspace_context();
            const auto& ws_name = ws_ctx.workspace_name.empty() ? container::String("default") : ws_ctx.workspace_name;
            auto sessions = agent_.history_db().list_sessions(ws_name);
            std::vector<container::String> result;
            result.reserve(sessions.size());
            for (const auto& s : sessions) {
                auto sid = s.value("session_id", "");
                if (!sid.empty()) {
                    result.emplace_back(sid.c_str());
                }
            }
            return result;
        }
        return {};
    });

    editor_.set_completer(std::move(completer));

    for (;;) {
        auto line = editor_.read_line();

        if (line.empty()) continue;

        if (line == std::string(LineEditor::kInterrupted)) {
            ++interrupt_count_;
            if (interrupt_count_ >= 2) {
                std::cout << "\n";
                break;
            }
            std::cout << "\n(再按 Ctrl+C 退出)\n";
            continue;
        }
        interrupt_count_ = 0;

        if (base::utils::trim(line).empty()) continue;

        if (line[0] == '/' && handle_command(line)) continue;

        if (line == "/exit" || line == "/quit") break;

        if (!send_message(line)) break;
    }

    editor_.save_history();
    return 0;
}

void ChatRepl::register_commands() {
    // 命令通过 SlashCompleter 注册，这里只做逻辑分发
}

bool ChatRepl::handle_command(const std::string& line) {
    // /exit 和 /quit 不在 handle_command 中处理，返回 false 让外层 break
    if (line == "/exit" || line == "/quit") return false;

    auto space_pos = line.find(' ');
    auto cmd = (space_pos == std::string::npos) ? line : line.substr(0, space_pos);
    auto args = (space_pos == std::string::npos) ? std::string{} : line.substr(space_pos + 1);
    while (!args.empty() && args.front() == ' ') args.erase(0, 1);

    if (cmd == "/help") {
        std::cout << "Commands:\n"
                  << "  /exit        - 退出\n"
                  << "  /help        - 显示帮助\n"
                  << "  /new         - 创建新会话\n"
                  << "  /sessions    - 列出历史会话\n"
                  << "  /resume <id> - 恢复历史会话（Tab 补全 ID）\n"
                  << "  /compact     - 手动上下文压缩\n"
                  << "  /clear       - 清屏\n"
                  << "  /model       - 显示当前模型\n";
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

    if (cmd == "/resume") {
        if (args.empty()) {
            std::cerr << "Usage: /resume <session_id>\n";
            return true;
        }
        log::info_fmt("resuming session: id={}", args);
        std::cout << "Session resume requested: " << args << "\n";
        return true;
    }

    if (cmd == "/new") {
        log::info_fmt("creating new session");
        std::cout << "New session requested\n";
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

    std::cerr << "Unknown command: " << cmd << "\n";
    return true;
}

bool ChatRepl::send_message(const std::string& prompt) {
    auto& io_loop = agent_.resources()->io_context()->loop();
    auto& callbacks = cli_app_->callbacks();

    log::info_fmt("chat request received stream={}", agent_.settings().stream ? "true" : "false");

    net::CancellationToken cancel;
    // 请求期间退出 raw mode，让 Ctrl+C 产生 SIGINT 取消请求
    editor_.suspend_raw_mode();
    
    // 注册 SIGINT 处理器，直接触发 CancellationToken 取消
    // 使用全局变量存储 cancel 指针，信号处理器中访问
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
    
    // 恢复 SIGINT 处理
    ::signal(SIGINT, prev_handler);
    g_cancel_ptr.reset();
    return true;
}

}  // namespace ben_gear
