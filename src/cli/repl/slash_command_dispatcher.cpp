#include "ben_gear/cli/repl/slash_command_dispatcher.hpp"

#include "ben_gear/agent/agent.hpp"
#include "ben_gear/agent/plan_manager.hpp"
#include "ben_gear/cli/render/cli_app.hpp"
#include "ben_gear/cli/render/highlight.hpp"
#include "ben_gear/cli/render/terminal.hpp"
#include "ben_gear/cli/render/theme.hpp"
#include "ben_gear/base/log/logger.hpp"
#include "ben_gear/base/utils/string_utils.hpp"
#include "ben_gear/config/settings.hpp"
#include "ben_gear/tools/history_tools.hpp"
#include "ben_gear/workspace/history_exporter.hpp"
#include "ben_gear/workspace/session.hpp"

#include <cstdio>
#include <ctime>
#include <iostream>
#include <set>

namespace ben_gear::cli {

SlashCommandDispatcher::SlashCommandDispatcher(SlashCommandContext context)
    : context_(std::move(context)) {}

bool SlashCommandDispatcher::dispatch(const std::string& line) {
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
            << " /history [n]         - 显示最近 n 条历史消息（默认 20）\n"
            << " /history delete ... - 删除历史（all|before|after|keyword|session|messages）\n"
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
        auto& ws_ctx = context_.agent.resources()->workspace_context();
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
            std::tm tm{};
#if defined(_WIN32)
            const bool tm_ok = localtime_s(&tm, &now) == 0;
#else
            const bool tm_ok = localtime_r(&now, &tm) != nullptr;
#endif
            if (tm_ok) std::strftime(buf, sizeof(buf), "history_%Y%m%d_%H%M%S.md", &tm);
            filename = buf;
        }

        bool ok = workspace::HistoryExporter::export_session_to_file(
            context_.agent.history_db(), ws_name, context_.session.session_id(), filename, opts);
        if (ok) {
            std::cout << "Exported to: " << filename << "\n";
        } else {
            std::cerr << "Export failed.\n";
        }
        return true;
    }

    if (cmd == "/plan") {
        auto& pm = context_.agent.plan_manager();
        auto& event_sink = context_.cli_app.event_sink();

        // 解析子命令
        auto space_pos = args.find(' ');
        auto subcmd = (space_pos == std::string::npos) ? args : args.substr(0, space_pos);

        if (subcmd == "off") {
            if (pm.in_plan_mode()) {
                pm.exit_plan_mode();
                event_sink.on_mode_changed(PlanManager::Mode::normal);
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
            event_sink.on_mode_changed(PlanManager::Mode::planning);
            log::info_fmt("plan mode entered");
        }
        return true;
    }

    if (cmd == "/compact") {
        log::info_fmt("manual compact triggered");
        auto& io_loop = context_.agent.resources()->io_context()->loop();
        auto before = context_.session.history().size();
        context_.session.maybe_compact(io_loop, context_.agent.resources()->provider(), context_.agent.resources()->tools());
        auto after = context_.session.history().size();
        std::cout << "Compacted: " << before << " -> " << after << " messages\n";
        return true;
    }

    if (cmd == "/clear") {
        fwrite("\033[2J\033[H", 7, 1, stdout);
        fflush(stdout);
        return true;
    }

    if (cmd == "/model") {
        auto& settings = context_.agent.settings();
        std::cout << "Model: " << std::string(settings.model.data(), settings.model.size()) << "\n";
        std::cout << "Provider: " << provider_name(settings.provider) << "\n";
        return true;
    }


    if (cmd == "/sessions") {
        auto& ws_ctx = context_.agent.resources()->workspace_context();
        const auto& ws_name = ws_ctx.workspace_name.empty() ? container::String("default") : ws_ctx.workspace_name;
        auto sessions = context_.agent.history_db().list_sessions(ws_name, agent::SessionType::main);
        if (sessions.empty()) {
            std::cout << "No sessions found.\n";
        } else {
            std::cout << "Sessions (" << sessions.size() << "):\n";
            for (const auto& s : sessions) {
                auto sid = s.value("context_.sessionid", "");
                auto updated = s.value("updated_at", "");
                auto count = s.value("msg_count", 0);
                auto current_sid = std::string(context_.session.session_id().data(), context_.session.session_id().size());
                std::cout << "  " << sid;
                if (sid == current_sid) std::cout << " *";
                std::cout << "  msgs=" << count << "  " << updated << "\n";
            }
        }
        return true;
    }

    if (cmd == "/history") {
        // /history delete 子指令路由
        if (!args.empty() && args.substr(0, 6) == "delete") {
            auto sub_args = args.size() > 7 ? args.substr(7) : "";
            while (!sub_args.empty() && sub_args.front() == ' ') sub_args.erase(0, 1);

            auto& ws_ctx = context_.agent.resources()->workspace_context();
            const auto& ws_name = ws_ctx.workspace_name.empty() ? container::String("default") : ws_ctx.workspace_name;
            auto& db = context_.agent.history_db();

            if (!context_.confirm_delete) {
                std::cerr << "Delete confirmation is not available.\n";
                return true;
            }
            auto& confirm_delete = context_.confirm_delete;

            // 解析子命令
            auto space_pos = sub_args.find(' ');
            auto subcmd = (space_pos == std::string::npos) ? sub_args : sub_args.substr(0, space_pos);
            auto sub_arg = (space_pos == std::string::npos) ? std::string{} : sub_args.substr(space_pos + 1);
            while (!sub_arg.empty() && sub_arg.front() == ' ') sub_arg.erase(0, 1);

            // /history delete messages before|keyword ...
            if (subcmd == "messages") {
                auto mspace = sub_arg.find(' ');
                auto msub = (mspace == std::string::npos) ? sub_arg : sub_arg.substr(0, mspace);
                auto marg = (mspace == std::string::npos) ? std::string{} : sub_arg.substr(mspace + 1);
                while (!marg.empty() && marg.front() == ' ') marg.erase(0, 1);

                if (msub == "before" && !marg.empty()) {
                    auto ts = tools::parse_time_string(marg);
                    if (ts == 0) { std::cerr << "Invalid time: " << marg << "\n"; return true; }
                    auto total = db.count_session_messages(ws_name, context_.session.session_id());
                    if (confirm_delete("将删除当前会话中 " + std::to_string(total) + " 条消息里 " + marg + " 之前的消息")) {
                        int deleted = db.delete_messages_before(ws_name, context_.session.session_id(), ts);
                        auto remaining = db.count_session_messages(ws_name, context_.session.session_id());
                        std::cout << "Deleted " << deleted << " messages (was " << total << ", now " << remaining << ")\n";
                    } else {
                        std::cout << "Cancelled.\n";
                    }
                } else if (msub == "keyword" && !marg.empty()) {
                    auto total = db.count_session_messages(ws_name, context_.session.session_id());
                    int deleted = db.delete_messages_by_keyword(ws_name, context_.session.session_id(), container::String(marg.c_str()));
                    auto remaining = db.count_session_messages(ws_name, context_.session.session_id());
                    std::cout << "Deleted " << deleted << " messages with keyword '" << marg << "' (was " << total << ", now " << remaining << ")\n";
                } else {
                    std::cerr << "Usage: /history delete messages before <date>|keyword <kw>\n";
                }
                return true;
            }

            if (subcmd == "all") {
                auto sessions = db.list_sessions(ws_name);
                auto total = db.count_messages(ws_name);
                if (confirm_delete("将删除 " + std::to_string(sessions.size()) + " 个会话 (" + std::to_string(total) + " 条消息)")) {
                    int deleted = db.delete_all_sessions(ws_name);
                    std::cout << "Deleted " << deleted << " sessions.\n";
                } else {
                    std::cout << "Cancelled.\n";
                }
            } else if (subcmd == "before" && !sub_arg.empty()) {
                auto ts = tools::parse_time_string(sub_arg);
                if (ts == 0) { std::cerr << "Invalid time: " << sub_arg << "\n"; return true; }
                auto sessions = db.list_sessions(ws_name);
                int match = 0;
                for (const auto& s : sessions) {
                    auto updated = s.value("updated_at", "");
                    if (updated.size() >= 10) {
                        auto s_ts = tools::parse_time_string(std::string(updated.data(), updated.size()).substr(0, 10));
                        if (s_ts > 0 && s_ts < ts) match++;
                    }
                }
                if (confirm_delete("将删除 " + std::to_string(match) + " 个会话 (updated before " + sub_arg + ")")) {
                    int deleted = db.delete_sessions_before(ws_name, ts);
                    std::cout << "Deleted " << deleted << " sessions.\n";
                } else {
                    std::cout << "Cancelled.\n";
                }
            } else if (subcmd == "after" && !sub_arg.empty()) {
                auto ts = tools::parse_time_string(sub_arg);
                if (ts == 0) { std::cerr << "Invalid time: " << sub_arg << "\n"; return true; }
                auto sessions = db.list_sessions(ws_name);
                int match = 0;
                for (const auto& s : sessions) {
                    auto updated = s.value("updated_at", "");
                    if (updated.size() >= 10) {
                        auto s_ts = tools::parse_time_string(std::string(updated.data(), updated.size()).substr(0, 10));
                        if (s_ts > 0 && s_ts > ts) match++;
                    }
                }
                if (confirm_delete("将删除 " + std::to_string(match) + " 个会话 (updated after " + sub_arg + ")")) {
                    int deleted = db.delete_sessions_after(ws_name, ts);
                    std::cout << "Deleted " << deleted << " sessions.\n";
                } else {
                    std::cout << "Cancelled.\n";
                }
            } else if (subcmd == "keyword" && !sub_arg.empty()) {
                auto results = db.search(container::String(sub_arg.c_str()), ws_name, 1000);
                std::set<std::string> ids;
                for (const auto& r : results) {
                    if (r.contains("context_.sessionid")) ids.insert(r["context_.sessionid"].get<std::string>());
                }
                if (confirm_delete("将删除 " + std::to_string(ids.size()) + " 个含 '" + sub_arg + "' 的会话")) {
                    int deleted = db.delete_sessions_by_keyword(ws_name, container::String(sub_arg.c_str()));
                    std::cout << "Deleted " << deleted << " sessions.\n";
                } else {
                    std::cout << "Cancelled.\n";
                }
            } else if (subcmd == "session" && !sub_arg.empty()) {
                // /history delete session [id] — 无 id 时默认当前会话
                auto sid = sub_arg.empty()
                    ? container::String(context_.session.session_id().data(), context_.session.session_id().size())
                    : container::String(sub_arg.c_str());
                auto sid_display = std::string(sid.data(), sid.size());
                auto msgs = db.load_session(ws_name, sid);
                if (confirm_delete("将删除会话 " + sid_display + " (" + std::to_string(msgs.size()) + " 条消息)")) {
                    db.delete_session(ws_name, sid);
                    std::cout << "Session deleted: " << sid_display << "\n";
                } else {
                    std::cout << "Cancelled.\n";
                }
            } else {
                // 默认：无子命令时删除当前会话
                if (subcmd.empty()) {
                    auto sid = container::String(context_.session.session_id().data(), context_.session.session_id().size());
                    auto sid_display = std::string(sid.data(), sid.size());
                    auto msgs = db.load_session(ws_name, sid);
                    if (confirm_delete("将删除当前会话 " + sid_display + " (" + std::to_string(msgs.size()) + " 条消息)")) {
                        db.delete_session(ws_name, sid);
                        std::cout << "Session deleted: " << sid_display << "\n";
                    } else {
                        std::cout << "Cancelled.\n";
                    }
                } else {
                    std::cerr << "Usage: /history delete [当前会话]|all|before <date>|after <date>|keyword <kw>|session [id]|messages before <date>|messages keyword <kw>\n";
                }
            }
            return true;
        }

        int n = 20;
        if (!args.empty()) {
            try { n = std::stoi(args); } catch (...) { n = 20; }
            if (n <= 0) n = 20;
        }
        auto& ws_ctx = context_.agent.resources()->workspace_context();
        const auto& ws_name = ws_ctx.workspace_name.empty() ? container::String("default") : ws_ctx.workspace_name;
        auto messages = context_.agent.history_db().load_session(ws_name, context_.session.session_id());
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
            std::cerr << "Usage: /resume <context_.sessionid>\n";
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
        auto& ws_ctx = context_.agent.resources()->workspace_context();
        const auto& ws_name = ws_ctx.workspace_name.empty() ? container::String("default") : ws_ctx.workspace_name;
        auto results = context_.agent.history_db().search(container::String(args.data(), args.size()), ws_name, 20);
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

}  // namespace ben_gear::cli
