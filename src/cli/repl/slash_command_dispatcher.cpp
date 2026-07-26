#include "cli/repl/slash_command_dispatcher.hpp"

#include "agent/runtime/runtime.hpp"
#include "orchestration/plan.hpp"
#include "orchestration/plan_parser.hpp"
#include "team/orchestrator.hpp"
#include "platform/os.hpp"
#include "llm/chat.hpp"
#include "net/event_loop.hpp"
#include "cli/render/cli_app.hpp"
#include "cli/render/highlight.hpp"
#include "cli/render/terminal.hpp"
#include "cli/render/theme.hpp"
#include "log/logger.hpp"
#include "base/utils/string_utils.hpp"
#include "config/settings.hpp"
#include "workspace/history_tools.hpp"
#include "workspace/history_exporter.hpp"
#include "workspace/session.hpp"
#include "agent/core/interfaces.hpp"
#include "llm/provider_client.hpp"

#include <cstdio>
#include <ctime>
#include <exception>
#include <filesystem>
#include <iostream>
#include <set>

namespace ben_gear::cli {

// ============================================================
// 构造：注册所有命令到 map
// ============================================================

SlashCommandDispatcher::SlashCommandDispatcher(SlashCommandContext context)
    : context_(std::move(context)) {

    using namespace std::placeholders;  // _1

    commands_ = {
        {"exit",    {"退出程序",         [this](auto a) { return cmd_help(a);      }}}, // placeholder — exit/quit handled in dispatch()
        {"quit",    {"退出程序",         [this](auto a) { return cmd_help(a);      }}},
        {"help",    {"显示帮助",         [this](auto a) { return cmd_help(a);      }}},
        {"h",       {"显示帮助（简写）", [this](auto a) { return cmd_help(a);      }}},
        {"exec",    {"执行系统命令",     [this](auto a) { return cmd_exec(a);      }}},
        {"file",    {"读取文件内容",     [this](auto a) { return cmd_file(a);      }}},
        {"export",  {"导出会话为 Markdown", [this](auto a) { return cmd_export(a); }}},
        {"plan",    {"计划模式（探索/off）", [this](auto a) { return cmd_plan(a);  }}},
        {"approve", {"批准计划",         [this](auto a) { return cmd_approve(a);   }}},
        {"cancel",  {"取消计划",         [this](auto a) { return cmd_cancel(a);    }}},
        {"compact", {"手动上下文压缩",   [this](auto a) { return cmd_compact(a);   }}},
        {"clear",   {"清屏",             [this](auto a) { return cmd_clear(a);     }}},
        {"model",   {"显示当前模型",     [this](auto a) { return cmd_model(a);     }}},
        {"sessions",{"列出历史会话",     [this](auto a) { return cmd_sessions(a);  }}},
        {"history", {"历史消息/删除",    [this](auto a) { return cmd_history(a);   }}},
        {"resume",  {"恢复历史会话（需重启）", [this](auto a) { return cmd_resume(a); }}},
        {"new",     {"创建新会话",       [this](auto a) { return cmd_new(a);       }}},
        {"search",  {"搜索历史消息",     [this](auto a) { return cmd_search(a);    }}},
        {"config",  {"显示当前配置",     [this](auto a) { return cmd_config(a);    }}},
        {"tools",   {"列出已注册工具",   [this](auto a) { return cmd_tools(a);     }}},
        {"team",    {"团队管理：list/create/run/assign/status", [this](auto a) { return cmd_team(a); }}},
    };
}

// ============================================================
// dispatch() — 查找命令并调用
// ============================================================

DispatchResult SlashCommandDispatcher::dispatch(const std::string& line) {
    // strip 前后空格
    auto trimmed = line;
    while (!trimmed.empty() && trimmed.front() == ' ') trimmed.erase(0, 1);
    while (!trimmed.empty() && trimmed.back() == ' ') trimmed.pop_back();

    auto space_pos = trimmed.find(' ');
    auto cmd = (space_pos == std::string::npos) ? trimmed : trimmed.substr(0, space_pos);
    auto args = (space_pos == std::string::npos) ? std::string{} : trimmed.substr(space_pos + 1);
    while (!args.empty() && args.front() == ' ') args.erase(0, 1);

    // /exit 和 /quit 直接退出
    if (cmd == "/exit" || cmd == "/quit") return DispatchResult::Exit;

    auto it = commands_.find(cmd.substr(1));  // 去掉前导 '/'
    if (it != commands_.end()) {
        return it->second.handler(std::string_view(args.data(), args.size()));
    }

    std::cerr << "Unknown command: " << cmd << "\n";
    return DispatchResult::Continue;
}

// ============================================================
// 命令处理函数
// ============================================================

DispatchResult SlashCommandDispatcher::cmd_help(std::string_view /*args*/) {
    // 按名称排序输出
    std::vector<std::pair<std::string, const CommandEntry*>> sorted;
    for (const auto& [name, entry] : commands_) {
        sorted.emplace_back(name, &entry);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    std::cout << "Commands:\n";
    for (const auto& [name, entry] : sorted) {
        // 跳过别名（h 是 help 的别名，quit 是 exit 的别名）
        if (name == "h" || name == "quit") continue;
        auto desc = entry->description;
        // 命令名 + 间距，保证对齐
        auto cmd_display = " /" + name;
        auto pad = cmd_display.size() < 16 ? 16 - cmd_display.size() : 1;
        std::cout << cmd_display << std::string(pad, ' ') << desc << "\n";
    }
    return DispatchResult::Continue;
}

// -----------------------------------------------------------

DispatchResult SlashCommandDispatcher::cmd_exec(std::string_view args) {
    auto* svc = context_.agent.services().resolve<agent::core::ICommandExecutor>();
    if (!svc) {
        std::cout << "command service not available\n";
    } else {
        auto r = svc->run(std::string(args));
        std::cout << (r.success() ? r.stdout_str : "exit=" + std::to_string(r.exit_code) + " " + r.stderr_str) << std::endl;
    }
    return DispatchResult::Continue;
}

// -----------------------------------------------------------

DispatchResult SlashCommandDispatcher::cmd_file(std::string_view args) {
    auto* svc = context_.agent.services().resolve<agent::core::IFileService>();
    if (!svc) {
        std::cout << "file service not available\n";
    } else {
        try {
            std::cout << svc->read(std::string(args)) << std::endl;
        } catch (const std::exception& e) {
            std::cout << "read failed: " << e.what() << std::endl;
        }
    }
    return DispatchResult::Continue;
}

// -----------------------------------------------------------

DispatchResult SlashCommandDispatcher::cmd_export(std::string_view args) {
    workspace::ExportOptions opts;
    std::string filename;
    auto arg_str = std::string(args);
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
        *context_.agent.services().resolve<workspace::HistoryDB>(), context_.session.session_id(), filename, opts);
    if (ok) {
        std::cout << "Exported to: " << filename << "\n";
    } else {
        std::cerr << "Export failed.\n";
    }
    return DispatchResult::Continue;
}

// -----------------------------------------------------------

DispatchResult SlashCommandDispatcher::cmd_plan(std::string_view args) {
    auto* pm = context_.agent.services().resolve<orchestration::PlanManager>();
    const auto& draft = pm->draft();

    if (args.empty()) {
        // 显示当前计划状态
        if (!pm->is_active()) {
            std::cout << "No active plan. Use /plan <description> to create one.\n";
        } else {
            std::cout << "Plan: " << draft.title << "\n";
            std::cout << "Status: " << to_string(draft.status) << " / " << to_string(draft.stage) << "\n";
            std::cout << "Items: " << draft.items.size() << "\n";
            if (!draft.items.empty()) {
                for (const auto& item : draft.items) {
                    std::cout << "  [" << item.order << "] " << item.title << "\n";
                }
            }
            std::cout << "\nCommands: /approve | /cancel | /plan off\n";
        }
        return DispatchResult::Continue;
    }

    if (args == "off" || args == "cancel") {
        pm->cancel();
        std::cout << "Plan cancelled.\n";
        return DispatchResult::Continue;
    }

    // 初始化计划状态机
    orchestration::PlanCommand pcmd;
    pcmd.session_id = context_.session.session_id();
    pcmd.workspace = context_.agent.services().resolve<workspace::WorkspaceContext>()->workspace_name;
    pcmd.prompt = std::string(args);
    pm->start(pcmd);

    // 生成计划
    auto* provider = context_.agent.services().resolve<llm::IProviderClient>();
    auto& loop = context_.agent.services().resolve<net::IoContext>()->loop();

    auto user_prompt = orchestration::build_plan_generation_prompt(std::string(args));
    llm::ChatRequest req;
    req.system_prompt = "You are a planning assistant. Return a structured JSON plan.";
    req.user_prompt = user_prompt;

    auto result = net::sync_wait(loop, provider->chat_async(loop, req));

    if (!result.ok()) {
        pm->mark_failed(result.error_message.empty()
            ? std::string("LLM request failed") : result.error_message);
        std::cout << "Plan generation failed: " << result.error_message << "\n";
        return DispatchResult::Continue;
    }

    auto& ws_name = context_.agent.services().resolve<workspace::WorkspaceContext>()->workspace_name;
    auto parsed = orchestration::parse_plan_draft_text(
        std::string_view(result.text.data(), result.text.size()),
        context_.session.session_id(), ws_name, std::string(args));

    if (!parsed.ok) {
        pm->mark_failed(parsed.error);
        std::cout << "Plan parsing failed: " << parsed.error << "\n";
        return DispatchResult::Continue;
    }

    pm->apply_model_draft(std::string(parsed.draft.title),
                         std::string(parsed.draft.objective),
                         std::move(parsed.draft.items));

    std::cout << "\n=== Plan ===\n";
    std::cout << "Title: " << pm->draft().title << "\n";
    for (const auto& item : pm->draft().items) {
        std::cout << "  [" << item.order << "] " << item.title << "\n";
    }
    std::cout << "\nReview the plan, chat to revise, then /approve or /cancel.\n";
    return DispatchResult::Continue;
}

// -----------------------------------------------------------

DispatchResult SlashCommandDispatcher::cmd_approve(std::string_view /*args*/) {
    auto* pm = context_.agent.services().resolve<orchestration::PlanManager>();
    if (!pm->is_reviewing()) {
        std::cout << "No plan to approve. Use /plan <description> first.\n";
        return DispatchResult::Continue;
    }
    try {
        pm->confirm_simple();
        pm->mark_executing();
        std::cout << "Plan approved! Tools are now available.\n";
    } catch (const std::exception& e) {
        std::cout << "Cannot approve: " << e.what() << "\n";
    }
    return DispatchResult::Continue;
}

// -----------------------------------------------------------

DispatchResult SlashCommandDispatcher::cmd_cancel(std::string_view /*args*/) {
    auto* pm = context_.agent.services().resolve<orchestration::PlanManager>();
    if (!pm->is_active()) {
        std::cout << "No active plan to cancel.\n";
        return DispatchResult::Continue;
    }
    pm->cancel();
    std::cout << "Plan cancelled.\n";
    return DispatchResult::Continue;
}

// -----------------------------------------------------------

DispatchResult SlashCommandDispatcher::cmd_compact(std::string_view /*args*/) {
    log::info_fmt("manual compact triggered");
    auto& agent_loop = context_.agent.services().resolve<net::IoContext>()->loop();
    auto before = context_.session.history().size();
    context_.session.maybe_compact(agent_loop, *context_.agent.services().resolve<llm::IProviderClient>(), *context_.agent.services().resolve<capabilities::tool::ToolRegistry>());
    auto after = context_.session.history().size();
    std::cout << "Compacted: " << before << " -> " << after << " messages\n";
    return DispatchResult::Continue;
}

// -----------------------------------------------------------

DispatchResult SlashCommandDispatcher::cmd_clear(std::string_view /*args*/) {
    fwrite(ansi::clear_screen().data(), ansi::clear_screen().size(), 1, stdout);
    fflush(stdout);
    return DispatchResult::Continue;
}

// -----------------------------------------------------------

DispatchResult SlashCommandDispatcher::cmd_model(std::string_view /*args*/) {
    auto* settings = context_.agent.services().resolve<config::Settings>();
    std::cout << "Model: " << std::string(settings->llm.model.data(), settings->llm.model.size()) << "\n";
    std::cout << "Provider: " << provider_name(settings->llm.provider) << "\n";
    return DispatchResult::Continue;
}

// -----------------------------------------------------------

DispatchResult SlashCommandDispatcher::cmd_sessions(std::string_view /*args*/) {
    auto* ws_ctx = context_.agent.services().resolve<workspace::WorkspaceContext>();
    const auto& ws_name = ws_ctx->workspace_name.empty() ? std::string("default") : ws_ctx->workspace_name;
    const auto& user = ws_ctx->username;
    auto sessions = context_.agent.services().resolve<workspace::HistoryDB>()->list_sessions(user, ws_name, config::SessionType::main);
    if (sessions.empty()) {
        std::cout << "No sessions found.\n";
    } else {
        std::cout << "Sessions (" << sessions.size() << "):\n";
        for (const auto& s : sessions) {
            auto sid = s.value("session_id", "");
            auto updated = s.value("updated_at", "");
            auto count = s.value("msg_count", 0);
            auto current_sid = std::string(context_.session.session_id().data(), context_.session.session_id().size());
            std::cout << "  " << sid;
            if (sid == current_sid) std::cout << " *";
            std::cout << "  msgs=" << count << "  " << updated << "\n";
        }
    }
    return DispatchResult::Continue;
}

// -----------------------------------------------------------

DispatchResult SlashCommandDispatcher::cmd_history(std::string_view args) {
    // /history delete 子指令路由
    if (!args.empty() && args.substr(0, 6) == "delete") {
        auto sub_args = args.size() > 7 ? args.substr(7) : "";
        // strip leading spaces
        auto sa = std::string(sub_args);
        while (!sa.empty() && sa.front() == ' ') sa.erase(0, 1);
        auto* ws_ctx = context_.agent.services().resolve<workspace::WorkspaceContext>();
        const auto& ws_name = ws_ctx->workspace_name.empty() ? std::string("default") : ws_ctx->workspace_name;
        const auto& user = ws_ctx->username;
        auto& db = *context_.agent.services().resolve<workspace::HistoryDB>();
        if (!context_.confirm_delete) {
            std::cerr << "Delete confirmation is not available.\n";
            return DispatchResult::Continue;
        }
        auto& confirm_delete = context_.confirm_delete;

        // 解析子命令
        auto pos = sa.find(' ');
        auto subcmd = (pos == std::string::npos) ? sa : sa.substr(0, pos);
        auto sub_arg = (pos == std::string::npos) ? std::string{} : sa.substr(pos + 1);
        while (!sub_arg.empty() && sub_arg.front() == ' ') sub_arg.erase(0, 1);

        // /history delete messages before|keyword ...
        if (subcmd == "messages") {
            auto mspace = sub_arg.find(' ');
            auto msub = (mspace == std::string::npos) ? sub_arg : sub_arg.substr(0, mspace);
            auto marg = (mspace == std::string::npos) ? std::string{} : sub_arg.substr(mspace + 1);
            while (!marg.empty() && marg.front() == ' ') marg.erase(0, 1);

            if (msub == "before" && !marg.empty()) {
                auto ts = workspace::parse_time_string(marg);
                if (ts == 0) { std::cerr << "Invalid time: " << marg << "\n"; return DispatchResult::Continue; }
                auto total = db.count_session_messages(context_.session.session_id());
                if (confirm_delete("将删除当前会话中 " + std::to_string(total) + " 条消息里 " + marg + " 之前的消息")) {
                    int deleted = db.delete_messages_before(context_.session.session_id(), ts);
                    auto remaining = db.count_session_messages(context_.session.session_id());
                    std::cout << "Deleted " << deleted << " messages (was " << total << ", now " << remaining << ")\n";
                } else {
                    std::cout << "Cancelled.\n";
                }
            } else if (msub == "keyword" && !marg.empty()) {
                auto total = db.count_session_messages(context_.session.session_id());
                int deleted = db.delete_messages_by_keyword(context_.session.session_id(), marg);
                auto remaining = db.count_session_messages(context_.session.session_id());
                std::cout << "Deleted " << deleted << " messages with keyword '" << marg << "' (was " << total << ", now " << remaining << ")\n";
            } else {
                std::cerr << "Usage: /history delete messages before <date>|keyword <kw>\n";
            }
            return DispatchResult::Continue;
        }

        if (subcmd == "all") {
            auto sessions = db.list_sessions(user, ws_name);
            auto total = db.count_messages(user, ws_name);
            if (confirm_delete("将删除 " + std::to_string(sessions.size()) + " 个会话 (" + std::to_string(total) + " 条消息)")) {
                int deleted = db.delete_all_sessions(user, ws_name);
                std::cout << "Deleted " << deleted << " sessions.\n";
            } else {
                std::cout << "Cancelled.\n";
            }
        } else if (subcmd == "before" && !sub_arg.empty()) {
            auto ts = workspace::parse_time_string(sub_arg);
            if (ts == 0) { std::cerr << "Invalid time: " << sub_arg << "\n"; return DispatchResult::Continue; }
            auto sessions = db.list_sessions(user, ws_name);
            int match = 0;
            for (const auto& s : sessions) {
                auto updated = s.value("updated_at", "");
                if (updated.size() >= 10) {
                    auto s_ts = workspace::parse_time_string(std::string(updated.data(), updated.size()).substr(0, 10));
                    if (s_ts > 0 && s_ts < ts) match++;
                }
            }
            if (confirm_delete("将删除 " + std::to_string(match) + " 个会话 (updated before " + sub_arg + ")")) {
                int deleted = db.delete_sessions_before(user, ws_name, ts);
                std::cout << "Deleted " << deleted << " sessions.\n";
            } else {
                std::cout << "Cancelled.\n";
            }
        } else if (subcmd == "after" && !sub_arg.empty()) {
            auto ts = workspace::parse_time_string(sub_arg);
            if (ts == 0) { std::cerr << "Invalid time: " << sub_arg << "\n"; return DispatchResult::Continue; }
            auto sessions = db.list_sessions(user, ws_name);
            int match = 0;
            for (const auto& s : sessions) {
                auto updated = s.value("updated_at", "");
                if (updated.size() >= 10) {
                    auto s_ts = workspace::parse_time_string(std::string(updated.data(), updated.size()).substr(0, 10));
                    if (s_ts > 0 && s_ts > ts) match++;
                }
            }
            if (confirm_delete("将删除 " + std::to_string(match) + " 个会话 (updated after " + sub_arg + ")")) {
                int deleted = db.delete_sessions_after(user, ws_name, ts);
                std::cout << "Deleted " << deleted << " sessions.\n";
            } else {
                std::cout << "Cancelled.\n";
            }
        } else if (subcmd == "keyword" && !sub_arg.empty()) {
            auto results = db.search(sub_arg, user, ws_name, 1000);
            std::set<std::string> ids;
            for (const auto& r : results) {
                if (r.contains("session_id")) ids.insert(r["session_id"].get<std::string>());
            }
            if (confirm_delete("将删除 " + std::to_string(ids.size()) + " 个含 '" + sub_arg + "' 的会话")) {
                int deleted = db.delete_sessions_by_keyword(user, ws_name, sub_arg);
                std::cout << "Deleted " << deleted << " sessions.\n";
            } else {
                std::cout << "Cancelled.\n";
            }
        } else if (subcmd == "session" && !sub_arg.empty()) {
            auto sid = sub_arg;
            auto sid_display = std::string(sid.data(), sid.size());
            auto msgs = db.load_session(sid);
            if (confirm_delete("将删除会话 " + sid_display + " (" + std::to_string(msgs.size()) + " 条消息)")) {
                db.delete_session(sid);
                auto sess_dir = ws_ctx->tier_paths.workspace_dir / "sessions" / sid_display;
                std::error_code ec;
                std::filesystem::remove_all(sess_dir, ec);
                std::cout << "Session deleted: " << sid_display << "\n";
            } else {
                std::cout << "Cancelled.\n";
            }
        } else {
            // 默认：无子命令时只删除消息，保留会话
            if (subcmd.empty()) {
                auto sid = std::string(context_.session.session_id().data(), context_.session.session_id().size());
                auto total = db.count_session_messages(sid);
                if (total == 0) {
                    std::cout << "No messages to delete.\n";
                } else if (confirm_delete("将删除当前会话全部 " + std::to_string(total) + " 条消息（会话保留）")) {
                    db.delete_session(sid);
                    db.create_session(user, ws_name, sid, std::string());
                    auto sess_dir = ws_ctx->tier_paths.workspace_dir / "sessions" / std::string(sid.data(), sid.size());
                    std::error_code ec;
                    std::filesystem::remove_all(sess_dir, ec);
                    context_.session.history().clear();
                    std::cout << "Deleted " << total << " messages.\n";
                } else {
                    std::cout << "Cancelled.\n";
                }
            } else {
                std::cerr << "Usage: /history delete [当前会话]|all|before <date>|after <date>|keyword <kw>|session [id]|messages before <date>|messages keyword <kw>\n";
            }
        }
        return DispatchResult::Continue;
    }

    // /history [n] — 显示最近 n 条消息
    int n = 20;
    if (!args.empty()) {
        try { n = std::stoi(std::string(args)); } catch (...) { n = 20; }
        if (n <= 0) n = 20;
    }
    auto messages = context_.agent.services().resolve<workspace::HistoryDB>()->load_session(context_.session.session_id());
    if (messages.empty()) {
        std::cout << "No history messages.\n";
        return DispatchResult::Continue;
    }
    int start = static_cast<int>(messages.size()) - n;
    if (start < 0) start = 0;
    auto theme = cli::Theme::default_dark();
    auto cap = cli::detect_terminal();
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
    return DispatchResult::Continue;
}

// -----------------------------------------------------------

DispatchResult SlashCommandDispatcher::cmd_resume(std::string_view args) {
    if (args.empty()) {
        std::cerr << "Usage: /resume <session_id>\n";
        return DispatchResult::Continue;
    }
    log::info_fmt("session resume requested: id={}", args);
    std::cout << "Session resume requested: " << args
              << "\n（会话切换需重启，请使用 bengear --session=" << args << "）\n";
    return DispatchResult::Continue;
}

// -----------------------------------------------------------

DispatchResult SlashCommandDispatcher::cmd_new(std::string_view /*args*/) {
    log::info_fmt("new session requested");
    std::cout << "New session will be created on next restart.\n"
              << "Tip: use 'bengear --new-session' to force a fresh session.\n";
    return DispatchResult::Continue;
}

// -----------------------------------------------------------

DispatchResult SlashCommandDispatcher::cmd_search(std::string_view args) {
    if (args.empty()) {
        std::cerr << "Usage: /search <keyword>\n";
        return DispatchResult::Continue;
    }
    auto* ws_ctx = context_.agent.services().resolve<workspace::WorkspaceContext>();
    const auto& ws_name = ws_ctx->workspace_name.empty() ? std::string("default") : ws_ctx->workspace_name;
    const auto& user = ws_ctx->username;
    auto results = context_.agent.services().resolve<workspace::HistoryDB>()->search(std::string(args.data(), args.size()), user, ws_name, 20);
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
    return DispatchResult::Continue;
}

// -----------------------------------------------------------

DispatchResult SlashCommandDispatcher::cmd_config(std::string_view /*args*/) {
    auto* settings = context_.agent.services().resolve<config::Settings>();
    std::cout << "provider=" << provider_name(settings->llm.provider) << '\n'
              << "model=" << std::string(settings->llm.model.data(), settings->llm.model.size()) << '\n'
              << "stream=" << (settings->llm.stream ? "true" : "false") << '\n'
              << "temperature=" << settings->llm.temperature << '\n'
              << "max_tokens=" << settings->llm.max_tokens << '\n'
              << "context_length=" << settings->llm.context_length << '\n'
              << "agent.max_tool_steps=" << settings->agent.max_tool_steps << '\n'
              << "agent.max_tool_calls=" << settings->agent.max_tool_calls << '\n'
              << "agent.max_tool_calls_per_step=" << settings->agent.max_tool_calls_per_step << '\n'
              << "connection_pool.max_connections_per_host=" << settings->connection_pool.max_connections_per_host << '\n';
    return DispatchResult::Continue;
}

// -----------------------------------------------------------

DispatchResult SlashCommandDispatcher::cmd_tools(std::string_view /*args*/) {
    const auto& names = context_.agent.services().resolve<capabilities::tool::ToolRegistry>()->tool_names();
    if (names.empty()) {
        std::cout << "No tools registered.\n";
    } else {
        std::cout << "Tools (" << names.size() << "):\n";
        for (const auto& name : names) {
            std::cout << "  " << name << "\n";
        }
    }
    return DispatchResult::Continue;
}

// ═══════════════════════════════════════════════════════════════════
//  /team — 团队管理
// ═══════════════════════════════════════════════════════════════════

DispatchResult SlashCommandDispatcher::cmd_team(std::string_view args) {
    auto* orch = context_.agent.services().resolve<team::TeamOrchestrator>();
    if (!orch) {
        std::cout << "Team system not initialized.\n";
        return DispatchResult::Continue;
    }

    // 解析子命令
    auto s = std::string(args);
    auto space = s.find(' ');
    auto sub = (space == std::string::npos) ? s : s.substr(0, space);
    auto rest = (space == std::string::npos) ? std::string() : s.substr(space + 1);

    if (sub == "list" || sub.empty()) {
        // /team list — 列出所有团队
        auto teams = orch->list_teams();
        if (teams.empty()) {
            std::cout << "No teams loaded. Use /team create <name> to load one.\n";
        } else {
            std::cout << "Teams (" << teams.size() << "):\n";
            for (const auto& id : teams) {
                auto status = orch->get_status(id);
                std::cout << "  " << id;
                if (status) {
                    std::cout << " [" << (status->running ? "running" : "idle") << "]";
                    std::cout << " stage=" << status->current_stage;
                    std::cout << " members=" << status->members.size();
                }
                std::cout << "\n";
            }
        }
        return DispatchResult::Continue;
    }

    if (sub == "create") {
        // /team create <name>
        auto name = std::string(args.substr(sub.size() + 1));
        while (!name.empty() && name.front() == ' ') name.erase(name.begin());
        if (name.empty()) {
            std::cout << "Usage: /team create <name>\n";
            return DispatchResult::Continue;
        }

        auto teams_dir = std::filesystem::path(
            ben_gear::base::platform::os::data_directory()) / "teams";
        if (orch->register_team(teams_dir, name)) {
            std::cout << "Team '" << name << "' loaded.\n";
        } else {
            std::cout << "Failed to load team '" << name << "'. Check ~/.bengear/teams/" << name << "/\n";
        }
        return DispatchResult::Continue;
    }

    if (sub == "run") {
        // /team run <name> <objective>
        auto rest_str = std::string(args.substr(sub.size() + 1));
        while (!rest_str.empty() && rest_str.front() == ' ') rest_str.erase(rest_str.begin());
        auto sp = rest_str.find(' ');
        if (sp == std::string::npos) {
            std::cout << "Usage: /team run <name> <objective>\n";
            return DispatchResult::Continue;
        }
        auto tname = rest_str.substr(0, sp);
        auto obj = rest_str.substr(sp + 1);
        auto exec_id = orch->start(tname, obj);
        if (!exec_id.empty()) {
            std::cout << "Team '" << tname << "' started (exec_id=" << exec_id << ")\n";
        } else {
            std::cout << "Failed to start team '" << tname << "'\n";
        }
        return DispatchResult::Continue;
    }

    if (sub == "assign") {
        // /team assign <name> <member> <task>
        auto rest_str = std::string(args.substr(sub.size() + 1));
        while (!rest_str.empty() && rest_str.front() == ' ') rest_str.erase(rest_str.begin());
        auto sp1 = rest_str.find(' ');
        if (sp1 == std::string::npos) {
            std::cout << "Usage: /team assign <name> <member> <task>\n";
            return DispatchResult::Continue;
        }
        auto tname = rest_str.substr(0, sp1);
        auto after_team = rest_str.substr(sp1 + 1);
        auto sp2 = after_team.find(' ');
        if (sp2 == std::string::npos) {
            std::cout << "Usage: /team assign <name> <member> <task>\n";
            return DispatchResult::Continue;
        }
        auto member = after_team.substr(0, sp2);
        auto task = after_team.substr(sp2 + 1);
        if (orch->dispatch(tname, member, task)) {
            std::cout << "Task assigned to " << member << " in " << tname << ".\n";
        } else {
            std::cout << "Failed to assign task.\n";
        }
        return DispatchResult::Continue;
    }

    if (sub == "status") {
        // /team status <name>
        auto name = std::string(args.substr(sub.size() + 1));
        while (!name.empty() && name.front() == ' ') name.erase(name.begin());
        if (name.empty()) {
            std::cout << "Usage: /team status <name>\n";
            return DispatchResult::Continue;
        }
        auto status = orch->get_status(name);
        if (!status) {
            std::cout << "Team '" << name << "' not found.\n";
            return DispatchResult::Continue;
        }
        std::cout << "Team: " << name
                  << " [" << (status->running ? "running" : "idle") << "]"
                  << " stage=" << status->current_stage << "\n";
        std::cout << "Members:\n";
        for (const auto& m : status->members) {
            const char* states[] = {"idle", "busy", "sleeping"};
            std::cout << "  " << m.agent_id << " (" << m.name << ")"
                      << " [" << states[static_cast<int>(m.state)] << "]";
            if (m.has_error) std::cout << " ERROR: " << m.last_error;
            std::cout << "\n";
        }
        return DispatchResult::Continue;
    }

    std::cout << "Unknown team subcommand: " << sub << "\n"
              << "Usage: /team [list|create|run|assign|status]\n";
    return DispatchResult::Continue;
}

}  // namespace ben_gear::cli
