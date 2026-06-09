#include "ben_gear/cli/repl/chat_repl.hpp"
#include "ben_gear/cli/render/cli_app.hpp"
#include "ben_gear/cli/render/terminal.hpp"
#include "ben_gear/cli/render/theme.hpp"

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

/// 打印时间戳，如 [14:32:05]
static void print_timestamp() {
    auto cap = cli::TerminalCapabilities::detect();
    auto now = std::time(nullptr);
    auto* tm = std::localtime(&now);
    char buf[10];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", tm);
    auto ts = ansi::colorize(std::string("[") + buf + "]", cli::Theme::default_dark().system_info, StyleFlag::dim, cap);
    std::cout << ts.c_str();
}

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
            {"history", "显示最近历史消息", true},
            {"resume",  "恢复历史会话",  true},
            {"plan",     "计划模式",      false},
            {"approve",  "确认执行计划",  false},
            {"steps",    "查看计划步骤",  false},
            {"skip",     "跳过当前步骤",  false},
            {"cancel",   "取消执行",      false},
            {"compact", "手动上下文压缩", false},
            {"clear",   "清屏",          false},
            {"model",   "显示当前模型",  true},
            {"search",  "搜索历史消息",  true},
            {"export",  "导出会话为 Markdown", true},
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
        // 根据计划模式动态更新提示符
        auto& pm = agent_.plan_manager();
        if (pm.in_plan_mode()) {
            editor_.set_prompt(config_.prompt + " [plan] ");
        } else if (pm.in_executing_mode()) {
            auto* cur = pm.current_step();
            if (cur) {
                editor_.set_prompt(config_.prompt + " [exec " + std::to_string(cur->index) + "/" + std::to_string(pm.total_steps()) + "] ");
            } else {
                editor_.set_prompt(config_.prompt + " [exec] ");
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
                  << "  /history [n] - 显示最近 n 条历史消息（默认 20）\n"
                  << "  /resume <id> - 恢复历史会话（Tab 补全 ID）\n"
                  << "  /plan        - 进入/退出计划模式\n"
                  << "  /approve     - 确认执行计划\n"
                  << "  /steps       - 查看计划步骤\n"
                  << "  /skip        - 跳过当前步骤\n"
                  << "  /cancel      - 取消执行\n"
                  << "  /compact     - 手动上下文压缩\n"
                  << "  /clear       - 清屏\n"
                  << "  /model       - 显示当前模型\n"
                  << "  /search <kw> - 搜索历史消息（FTS5 全文检索）\n"
                  << "  /export [file] - 导出当前会话为 Markdown\n";
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
        // 取最近 n 条
        int start = static_cast<int>(messages.size()) - n;
        if (start < 0) start = 0;
        auto theme = cli::Theme::default_dark();
        auto cap = cli::TerminalCapabilities::detect();
        for (int i = start; i < static_cast<int>(messages.size()); ++i) {
            auto& msg = messages[i];
            auto role = msg.value("role", "");
            auto content = msg.value("content", "");
            auto ts = msg.value("ts", "");
            // 只取时间部分 HH:MM:SS
            if (ts.size() >= 19) ts = "[" + ts.substr(11, 8) + "]";
            // 截断过长内容
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
            } else if (role == "tool_call") {
                // 工具调用：显示工具名称，content 是参数 JSON
                auto tool_name = msg.value("tool_name", "");
                auto name_str = tool_name.empty() ? "tool_call" : tool_name;
                auto lbl = ansi::colorize(name_str + " ", theme.tool_name, StyleFlag::none, cap);
                std::cout << ts_colored.c_str() << " " << lbl.c_str() << content << "\n";
            } else if (role == "tool") {
                // 工具结果：显示工具名称
                auto tool_name = msg.value("tool_name", "");
                auto name_str = tool_name.empty() ? "tool" : tool_name;
                auto lbl = ansi::colorize(name_str + " ", theme.tool_name, StyleFlag::none, cap);
                // 截断过长的工具结果
                auto display_content = content.size() > 120 ? content.substr(0, 120) + "..." : content;
                std::cout << ts_colored.c_str() << " " << lbl.c_str() << display_content << "\n";
            }
        }
        return true;
    }

    if (cmd == "/search") {
        if (args.empty()) {
            std::cerr << "Usage: /search <keyword>\n";
            return true;
        }
        auto& ws_ctx = agent_.resources()->workspace_context();
        const auto& ws_name = ws_ctx.workspace_name.empty() ? container::String("default") : ws_ctx.workspace_name;
        auto results = agent_.history_db().search(
            container::String(args.c_str()), ws_name, 20);
        if (results.empty()) {
            std::cout << "No results found.\n";
        } else {
            auto theme = cli::Theme::default_dark();
            auto cap = cli::TerminalCapabilities::detect();
            for (const auto& msg : results) {
                auto role = msg.value("role", "");
                auto content_str = msg.value("content", "");
                auto ts = msg.value("ts", "");
                if (ts.size() >= 19) ts = "[" + ts.substr(11, 8) + "]";
                auto tc_name = msg.value("tool_name", "");
                if (content_str.size() > 120) content_str = content_str.substr(0, 120) + "...";
                auto ts_colored = ansi::colorize(ts, theme.system_info, StyleFlag::dim, cap);
                std::string label;
                if (role == "user") label = "";
                else if (role == "assistant") label = ">> ";
                else if (role == "thinking") label = "?  ";
                else if (!tc_name.empty()) label = tc_name + " ";
                else label = role + " ";
                auto lbl_colored = ansi::colorize(label, theme.tool_name, StyleFlag::none, cap);
                std::cout << ts_colored.c_str() << " " << lbl_colored.c_str() << content_str << "\n";
            }
            std::cout << "--- " << results.size() << " results ---\n";
        }
        return true;
    }

    if (cmd == "/export") {
        auto& ws_ctx = agent_.resources()->workspace_context();
        const auto& ws_name = ws_ctx.workspace_name.empty() ? container::String("default") : ws_ctx.workspace_name;

        // 解析参数：/export [filename] [--no-tool] [--no-thinking] [--with-result]
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

        // 默认文件名
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

    if (cmd == "/plan") {
        auto& pm = agent_.plan_manager();
        if (args == "off") {
            pm.exit_plan_mode();
            cli_app_->callbacks().on_plan_mode_exited();
        } else if (pm.in_plan_mode()) {
            std::cout << "Already in plan mode.\n";
        } else if (pm.in_executing_mode()) {
            std::cout << "Currently executing. Use /cancel to stop.\n";
        } else {
            pm.enter_plan_mode();
            cli_app_->callbacks().on_plan_mode_entered();
        }
        return true;
    }

    if (cmd == "/approve") {
        auto& pm = agent_.plan_manager();
        if (!pm.in_plan_mode() && !pm.has_pending_auto_plan()) {
            std::cout << "No plan to approve.\n";
            return true;
        }
        auto& last_text = pm.last_plan_text();
        if (last_text.empty()) {
            std::cout << "No plan output yet. Describe your requirements first.\n";
            return true;
        }
        auto steps = PlanManager::parse_plan_from_text(
            std::string_view(last_text.data(), last_text.size()));
        if (steps.empty()) {
            std::cout << "Could not parse plan steps. Continue discussing to refine.\n";
            return true;
        }
        pm.set_steps(std::move(steps));
        // 显示计划（通过 renderer）
        cli_app_->callbacks().on_plan_detected(pm.steps());
        pm.approve();
        // 开始执行第一步
        execute_current_step();
        return true;
    }

    if (cmd == "/steps") {
        auto& pm = agent_.plan_manager();
        if (pm.steps().empty()) {
            std::cout << "No plan steps.\n";
        } else {
            cli_app_->callbacks().on_plan_detected(pm.steps());
        }
        return true;
    }

    if (cmd == "/skip") {
        auto& pm = agent_.plan_manager();
        if (!pm.in_executing_mode()) {
            std::cout << "Not in executing mode.\n";
            return true;
        }
        auto* step = pm.current_step();
        if (!step) {
            std::cout << "No current step to skip.\n";
            return true;
        }
        cli_app_->callbacks().on_step_skipped(*step);
        if (!pm.skip_step()) {
            finish_execution();
        } else {
            execute_current_step();
        }
        return true;
    }

    if (cmd == "/cancel") {
        auto& pm = agent_.plan_manager();
        if (pm.in_executing_mode() || pm.in_plan_mode()) {
            pm.exit_plan_mode();
            cli_app_->callbacks().on_plan_mode_exited();
        } else {
            std::cout << "Not in plan or executing mode.\n";
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

    std::cerr << "Unknown command: " << cmd << "\n";
    return true;
}

bool ChatRepl::send_message(const std::string& prompt) {
    // 显示用户消息时间+内容
    print_timestamp();
    std::cout << " " << prompt << "\n";

    // 执行模式：显示当前步骤
    auto& pm = agent_.plan_manager();
    if (pm.in_executing_mode()) {
        auto* step = pm.current_step();
        if (step) cli_app_->callbacks().on_step_started(*step, pm.total_steps());
    }

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

/// 执行当前计划步骤（迭代方式，避免递归栈溢出）
void ChatRepl::execute_current_step() {
    auto& pm = agent_.plan_manager();
    while (pm.in_executing_mode()) {
        auto* step = pm.current_step();
        if (!step) {
            finish_execution();
            return;
        }

        // 构造步骤执行 prompt
        auto desc = std::string(step->description.data(), step->description.size());
        auto prompt = "Execute step " + std::to_string(step->index) + "/" + std::to_string(pm.total_steps())
                      + ": " + desc;
        log::info_fmt("plan: executing step {}/{}: {}", step->index, pm.total_steps(), desc);
        send_message(prompt);

        // 步骤执行完成，标记完成并推进
        auto* cur = pm.current_step();
        if (cur) {
            cli_app_->callbacks().on_step_completed(*cur);
        }
        if (!pm.advance_step()) {
            finish_execution();
            return;
        }
        // 继续循环执行下一步
    }
}

/// 计划执行完毕，退出执行模式
void ChatRepl::finish_execution() {
    auto& pm = agent_.plan_manager();
    cli_app_->callbacks().on_plan_completed();
    pm.exit_plan_mode();
    log::info_fmt("plan: all steps completed, back to normal mode");
}

}  // namespace ben_gear
