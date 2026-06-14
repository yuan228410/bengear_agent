#include "ben_gear/ben_gear.hpp"
#include "ben_gear/cli/args.hpp"
#include "ben_gear/tools/history_tools.hpp"
#include "ben_gear/base/net/cancel.hpp"
#include "ben_gear/cli/render/cli_app.hpp"
#include "ben_gear/cli/repl/chat_repl.hpp"
#include "ben_gear/server/core/server.hpp"

#include <csignal>
#include <execinfo.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <set>

// 异步信号安全的终端恢复函数（定义在 terminal_io.cpp）
// 终端恢复函数（定义在 terminal_io.cpp，ben_gear::cli 命名空间）

namespace {

/// 全局取消令牌指针，供 SIGINT handler 使用
static ben_gear::CancellationToken* g_cancel_token = nullptr;

static void sigint_handler(int) {
    if (g_cancel_token) {
        g_cancel_token->cancel();
    }
}

static void install_sigint_handler(ben_gear::CancellationToken& token) {
    g_cancel_token = &token;
    std::signal(SIGINT, sigint_handler);
}

static void remove_sigint_handler() {
    std::signal(SIGINT, SIG_DFL);
    g_cancel_token = nullptr;
}

std::string join_prompt(const std::vector<std::string>& parts) {
    std::string prompt;
    for (const auto& part : parts) {
        if (!prompt.empty()) prompt.push_back(' ');
        prompt += part;
    }
    return prompt;
}

void print_config(const ben_gear::Config& config) {
    std::cout << "provider=" << ben_gear::provider_name(config.provider) << '\n'
              << "base_url=" << config.base_url << '\n'
              << "api_url=" << (config.api_url.empty() ? "<default>" : config.api_url) << '\n'
              << "model=" << config.model << '\n'
              << "stream=" << (config.stream ? "true" : "false") << '\n'
              << "llm_request_retry.max_attempts=" << config.llm_request_retry.max_attempts << '\n'
              << "llm_request_retry.initial_delay_ms=" << config.llm_request_retry.initial_delay_ms << '\n'
              << "llm_request_retry.max_delay_ms=" << config.llm_request_retry.max_delay_ms << '\n'
              << "log.level=" << ben_gear::level_name(config.logging.level) << '\n'
              << "log.output=" << config.logging.output << '\n'
              << "log.file=" << (config.logging.file.empty() ? ben_gear::log::default_log_file().string() : std::string(config.logging.file.c_str())) << '\n'
              << "context_length=" << config.context_length << '\n'
              << "max_tokens=" << config.max_tokens << '\n'
              << "temperature=" << config.temperature << '\n'
              << "headers=" << config.headers.size() << '\n'
              << "api_key=" << (config.api_key.empty() ? "<empty>" : "<set>") << '\n'
              << "agent.max_tool_steps=" << config.agent.max_tool_steps << '\n'
              << "agent.max_tool_calls=" << config.agent.max_tool_calls << '\n'
              << "agent.max_tool_calls_per_step=" << config.agent.max_tool_calls_per_step << '\n'
              << "agent.system_prompt=" << (config.agent.system_prompt.empty() ? "<default>" : "<custom>") << '\n'
              << "agent.command_timeout=" << config.agent.command_timeout << '\n'
              << "connection_pool.max_connections_per_host=" << config.connection_pool.max_connections_per_host << '\n'
              << "connection_pool.idle_timeout_seconds=" << config.connection_pool.idle_timeout_seconds << '\n'
              << "connection_pool.connect_timeout_seconds=" << config.connection_pool.connect_timeout_seconds << '\n'
              << "connection_pool.response_timeout_seconds=" << config.connection_pool.response_timeout_seconds << '\n'
              << "connection_pool.enable_keep_alive=" << (config.connection_pool.enable_keep_alive ? "true" : "false") << '\n'
              << "thread_pool.min_threads=" << config.thread_pool.min_threads << '\n'
              << "thread_pool.max_threads=" << config.thread_pool.max_threads << '\n'
              << "thread_pool.max_queue_size=" << config.thread_pool.max_queue_size << '\n'
              << "mcp.read_buffer_size=" << config.mcp.read_buffer_size << '\n'
              << "mcp_servers=" << config.mcp_servers.size() << '\n'
              << "anthropic_api_version=" << (config.anthropic_api_version.empty() ? "<default>" : std::string(config.anthropic_api_version.c_str())) << '\n'
              << "username=" << (config.username.empty() ? "default" : std::string(config.username.c_str())) << '\n'
              << "workspace_name=" << (config.workspace_name.empty() ? "default" : std::string(config.workspace_name.c_str())) << '\n'
              << "session_id=" << (config.session_id.empty() ? "<new>" : std::string(config.session_id.c_str())) << '\n';
    // 备用模型链
    std::cout << "fallback_models=";
    if (config.fallback_models.empty()) {
        std::cout << "<none>\n";
    } else {
        for (size_t i = 0; i < config.fallback_models.size(); ++i) {
            if (i > 0) std::cout << ", ";
            std::cout << config.fallback_models[i];
        }
        std::cout << '\n';
    }
    // 已解析的 fallback 配置
    std::cout << "resolved_fallbacks=" << config.resolved_fallbacks.size() << '\n';
    // 上下文裁剪配置
    std::cout << "context_prune.enabled=" << (config.context_prune.enabled ? "true" : "false") << '\n'
              << "context_prune.protect_recent=" << config.context_prune.protect_recent << '\n'
              << "context_prune.soft_prune_lines=" << config.context_prune.soft_prune_lines << '\n'
              << "context_prune.hard_prune_after=" << config.context_prune.hard_prune_after << '\n'
              << "context_prune.max_tool_result_chars=" << config.context_prune.max_tool_result_chars << '\n';
    for (const auto& [key, fb] : config.resolved_fallbacks) {
        std::cout << "  [" << key << "] provider="
                  << (fb.provider == ben_gear::Provider::anthropic ? "anthropic" : "openai")
                  << " model=" << fb.model
                  << " base_url=" << fb.base_url
                  << " api_key=" << (fb.api_key.empty() ? "<empty>" : "<set>")
                  << " max_tokens=" << fb.max_tokens
                  << " temperature=" << fb.temperature << '\n';
    }
}

/// 根据 config 构建 WorkspaceContext
ben_gear::workspace::WorkspaceContext build_ws_ctx(const ben_gear::Config& config) {
    namespace ws = ben_gear::workspace;
    namespace container = ben_gear::base::container;

    auto root = ben_gear::support::data_directory();
    auto username = config.username.empty() ? container::String("default") : config.username;
    auto ws_name = config.workspace_name.empty() ? container::String("default") : config.workspace_name;

    ws::TierPaths tier_paths{
        root,
        root / "users" / std::string(username.data(), username.size()),
        root / "users" / std::string(username.data(), username.size())
             / "workspaces" / std::string(ws_name.data(), ws_name.size())
    };

    return ws::WorkspaceContext{
        std::move(tier_paths),
        ws_name,
        container::String(config.workspace.string().c_str()),
        username,
        config.session_id
    };
}

/// 更新日志追踪标签
void update_trace_id(const ben_gear::workspace::WorkspaceContext& ws_ctx,
                      const ben_gear::workspace::Session& session) {
    std::string trace = std::string(ws_ctx.username.data(), ws_ctx.username.size()) + "-"
                 + std::string(ws_ctx.workspace_name.data(), ws_ctx.workspace_name.size()) + "-"
                 + std::string(session.session_id().data(), session.session_id().size());
    ben_gear::log::set_trace_id(std::move(trace));
}

int run_chat(const ben_gear::Config& config, bool /*stream*/, bool /*async_mode*/, bool md_raw = false, bool show_banner = true, bool force_new_session = false, bool no_thinking = false, bool no_tool = false, bool no_detail = false) {
    auto ws_ctx = build_ws_ctx(config);
    ben_gear::Agent agent(config, ws_ctx);

    // 交互模式：默认恢复最新会话，除非 force_new_session 或无历史会话
    auto session_id = config.session_id;
    if (session_id.empty() && !force_new_session) {
        auto sessions = agent.history_db().list_sessions(
            config.workspace_name.empty()
                ? ben_gear::base::container::String("default")
                : config.workspace_name);
        if (!sessions.empty()) {
            auto& latest = sessions[0];
            if (latest.contains("session_id")) {
                session_id = latest["session_id"].get<std::string>();
                ben_gear::log::info_fmt("auto-resume latest session: id={}", std::string(session_id));
            }
        }
    }

    // 创建 Session（可能恢复历史）
    auto session = std::make_unique<ben_gear::workspace::Session>(
        ben_gear::workspace::SessionConfig{session_id, agent.settings().context_length, agent.settings().context_prune, ben_gear::agent::SessionType::main, {}},
        agent.resources()->make_session_deps(), agent.resources()->tools_mut());
    if (!session_id.empty()) {
        session->restore_from_db(agent.history_db());
        ben_gear::log::info_fmt("session restored: id={}", std::string(session_id));
    }

    update_trace_id(ws_ctx, *session);

    ben_gear::cli::DisplayConfig display_cfg;
    if (md_raw) display_cfg.markdown_render = false;
    if (no_thinking || no_detail) display_cfg.show_thinking = false;
    if (no_tool || no_detail) { display_cfg.show_tool_call = false; display_cfg.show_tool_result = false; }
    auto cli_app = ben_gear::cli::CliApp::create(display_cfg,
        std::string_view(config.model.data(), config.model.size()),
        config.context_length);

    ben_gear::ChatRepl repl(agent, *session, std::move(cli_app),
        ben_gear::ChatRepl::Config{"", true, show_banner, !session_id.empty()});

    int rc = repl.run();
    return rc;
}

// ============ 崩溃信号处理器 ============
#include <dlfcn.h>
#include "ben_gear/base/log/logger.hpp"

static void crash_handler(int sig) {
    // 恢复终端状态（避免崩溃后终端卡在 raw mode）
    ben_gear::cli::restore_terminal_on_crash();

    // 重置信号处理器，避免递归
    signal(SIGSEGV, SIG_DFL);
    signal(SIGBUS, SIG_DFL);
    signal(SIGABRT, SIG_DFL);

    const char* sig_name = sig == SIGSEGV ? "SIGSEGV" : sig == SIGBUS ? "SIGBUS" : sig == SIGABRT ? "SIGABRT" : "UNKNOWN";
    char buf[512];
    snprintf(buf, sizeof(buf), "\n!!! CRASH: signal=%d (%s) !!!\n", sig, sig_name);
    write(STDERR_FILENO, buf, strlen(buf));

    void* frames[64];
    int n = backtrace(frames, 64);

    // 获取主模块加载基址
    Dl_info main_info{};
    void* base_addr = nullptr;
    const char* exe_path = nullptr;
    if (n > 0 && dladdr(frames[0], &main_info)) {
        base_addr = main_info.dli_fbase;
        exe_path = main_info.dli_fname;
    }

    // 输出每帧的地址和 dladdr 信息
    for (int i = 0; i < n; ++i) {
        Dl_info info{};
        if (dladdr(frames[i], &info) && info.dli_sname) {
            ptrdiff_t offset = static_cast<char*>(frames[i]) - static_cast<char*>(info.dli_saddr);
            snprintf(buf, sizeof(buf), "#%2d 0x%014lx  %s+%td  (%s)\n",
                     i, reinterpret_cast<uintptr_t>(frames[i]), info.dli_sname, offset,
                     info.dli_fname ? info.dli_fname : "?");
        } else {
            snprintf(buf, sizeof(buf), "#%2d 0x%014lx  ??\n", i, reinterpret_cast<uintptr_t>(frames[i]));
        }
        write(STDERR_FILENO, buf, strlen(buf));
    }

    // 输出 lldb 符号化命令
    if (exe_path) {
        write(STDERR_FILENO, "\n--- To resolve line numbers ---\n", 33);
        // lldb 批量命令
        std::string lldb_cmds;
        for (int i = 0; i < n && i < 30; ++i) {
            char cmd[128];
            snprintf(cmd, sizeof(cmd), "image lookup -a 0x%014lx\n", reinterpret_cast<uintptr_t>(frames[i]));
            lldb_cmds += cmd;
        }
        lldb_cmds += "quit\n";

        // 写入临时文件，lldb -s 读取
        char tmpfile[64];
        snprintf(tmpfile, sizeof(tmpfile), "/tmp/bengear_crash_%d.cmd", getpid());
        FILE* f = fopen(tmpfile, "w");
        if (f) {
            fwrite(lldb_cmds.c_str(), 1, lldb_cmds.size(), f);
            fclose(f);
            snprintf(buf, sizeof(buf), "lldb -s %s %s\n", tmpfile, exe_path);
            write(STDERR_FILENO, buf, strlen(buf));
        }

        // 也输出 atos 命令（某些环境 atos 更方便）
        snprintf(buf, sizeof(buf), "atos -arch arm64 -o %s", exe_path);
        write(STDERR_FILENO, buf, strlen(buf));
        if (base_addr) {
            snprintf(buf, sizeof(buf), " -l 0x%014lx", reinterpret_cast<uintptr_t>(base_addr));
            write(STDERR_FILENO, buf, strlen(buf));
        }
        for (int i = 0; i < n && i < 20; ++i) {
            snprintf(buf, sizeof(buf), " 0x%014lx", reinterpret_cast<uintptr_t>(frames[i]));
            write(STDERR_FILENO, buf, strlen(buf));
        }
        write(STDERR_FILENO, "\n", 1);
    }

    _exit(sig);
}

static void install_crash_handler() {
    signal(SIGSEGV, crash_handler);
    signal(SIGBUS, crash_handler);
    signal(SIGABRT, crash_handler);
}

}  // namespace

int main(int argc, char** argv) {
    install_crash_handler();
    try {
        namespace cli = ben_gear::cli;
        namespace container = ben_gear::base::container;
        namespace ws = ben_gear::workspace;

        std::filesystem::path workspace = std::filesystem::current_path();
        std::filesystem::path model_config;
        std::string active_model;
        bool use_stdin = false;
        bool show_config = false;
        
        bool stream_override = false;
        bool stream_value = true;
        bool async_mode = false;
        bool list_skills = false;
        bool new_session = false;
        bool md_raw = false;
    bool no_thinking = false;
    bool no_tool = false;
    bool no_detail = false;
        bool no_banner = false;
        std::vector<std::string> prompt_parts;

        ben_gear::Config config;
        bool loaded = false;
        auto ensure_loaded = [&] {
            if (!loaded) {
                config = ben_gear::load_config(workspace, model_config, active_model);
                loaded = true;
            }
        };

        cli::Parser parser;
        parser
            .prog("bengear - BenGear, a small C++20 cross-platform AI agent")
            .usage(
                "Usage:\n"
                "  bengear\n"
                "  bengear [options] <prompt>\n"
                "  bengear [options] --stdin\n"
                
                "  bengear workspace <list|create|remove|restore> [name]\n"
                "  bengear session <list|delete> [session_id]")
            .epilog(
                "Config precedence:\n"
                "  model json: <workspace>/config.json or --config\n"
                "  global:     platform config dir / bengear / global.conf\n"
                "  user:       ~/.bengear.conf\n"
                "  workspace:  <workspace>/.bengear.conf\n"
                "  env/cli:    BEN_GEAR_* and command-line options override files")
            .flag('h', "help", "Show help",
                  [&]{ parser.print_help(); std::exit(0); })
            .option('c', "config", "<path>", "JSON config file",
                    [&](std::string_view v){ model_config = v; })
            .option("active-model", "<name>", "Active model (name or provider:model)",
                    [&](std::string_view v){ active_model = v; })
            .option("workspace", "<path>", "Project workspace path",
                    [&](std::string_view v){ workspace = v; })
            // Multi-tier management options
            .option("user", "<name>", "Username (default: default)",
                    [&](std::string_view v){ ensure_loaded(); config.username = container::String(v.data()); })
            .option("workspace-name", "<name>", "Workspace name (default: default)",
                    [&](std::string_view v){ ensure_loaded(); config.workspace_name = container::String(v.data()); })
            .option("session", "<id>", "Resume session by ID",
                    [&](std::string_view v){ ensure_loaded(); config.session_id = container::String(v.data()); })
            .flag("new-session", "Force create a new session",
                  [&]{ new_session = true; })
            // Options below require config to be loaded
            .option("provider", "<name>", "openai|anthropic",
                    [&](std::string_view v){ ensure_loaded(); config.provider = ben_gear::parse_provider(v); })
            .option('m', "model", "<name>", "Model name",
                    [&](std::string_view v){ ensure_loaded(); config.model = container::String(v.data()); })
            .option("base-url", "<url>", "Base URL",
                    [&](std::string_view v){ ensure_loaded(); config.base_url = container::String(v.data()); })
            .option("api-url", "<url>", "API URL",
                    [&](std::string_view v){ ensure_loaded(); config.api_url = container::String(v.data()); })
            .option("api-key", "<key>", "API key",
                    [&](std::string_view v){ ensure_loaded(); config.api_key = container::String(v.data()); })
            .option("llm-request-retry-attempts", "<count>", "Retry attempts",
                    [&](std::string_view v){ ensure_loaded(); config.llm_request_retry.max_attempts = ben_gear::parse_positive_int(v, config.llm_request_retry.max_attempts); })
            .flag("stdin", "Read prompt from stdin", [&]{ use_stdin = true; })
            .flag("no-stream", "Disable streaming (default: streaming)", [&]{ stream_value = false; stream_override = true; })
            .flag('a', "async", "Use async mode", [&]{ async_mode = true; })
            .flag("sync", "Use sync mode", [&]{ async_mode = false; })
            .flag("show-config", "Print config and exit", [&]{ ensure_loaded(); show_config = true; })
            .flag("list-skills", "List skills and exit", [&]{ ensure_loaded(); list_skills = true; })
            .flag("md-raw", "Disable markdown rendering (show raw text)", [&]{ md_raw = true; })
            .flag("no-banner", "Disable startup banner", [&]{ no_banner = true; })
            .flag("no-thinking", "Hide thinking process", [&]{ no_thinking = true; })
            .flag("no-tool", "Hide tool calls", [&]{ no_tool = true; })
            .flag("no-detail", "Hide thinking and tool calls", [&]{ no_detail = true; })
            // workspace subcommand
            .command("workspace", "Workspace management", [&](const cli::Parsed& p) {
                ensure_loaded();
                auto ws_ctx = build_ws_ctx(config);
                ws::WorkspaceManager mgr(ws_ctx.tier_paths.user_dir);

                if (p.positional.empty()) {
                    std::cerr << "Usage: bengear workspace <list|create|remove|restore> [name]\n";
                    std::exit(1);
                }
                const auto& subcmd = p.positional[0];

                if (subcmd == "list") {
                    auto workspaces = mgr.list_all();
                    if (workspaces.empty()) {
                        std::cout << "No workspaces found.\n";
                    } else {
                        std::cout << "Workspaces (" << workspaces.size() << "):\n";
                        for (const auto& w : workspaces) {
                            std::cout << "  " << std::string(w.name.data(), w.name.size());
                            if (!std::string(w.project_path.data(), w.project_path.size()).empty()) {
                                std::cout << "  project=" << std::string(w.project_path.data(), w.project_path.size());
                            }
                            std::cout << "\n    dir=" << w.ws_dir.string() << "\n";
                        }
                    }
                } else if (subcmd == "create") {
                    if (p.positional.size() < 2) {
                        std::cerr << "Usage: bengear workspace create <name> [project_path]\n";
                        std::exit(1);
                    }
                    auto name = container::String(std::move(p.positional[1]));
                    container::String project_path;
                    if (p.positional.size() >= 3) {
                        project_path = container::String(std::move(p.positional[2]));
                    }
                    auto result = mgr.create(name, project_path);
                    if (result) {
                        std::cout << "Workspace created: " << p.positional[1] << "\n";
                    } else {
                        std::cerr << "Workspace already exists: " << p.positional[1] << "\n";
                        std::exit(1);
                    }
                } else if (subcmd == "remove") {
                    if (p.positional.size() < 2) {
                        std::cerr << "Usage: bengear workspace remove <name>\n";
                        std::exit(1);
                    }
                    auto name = container::String(std::move(p.positional[1]));
                    if (mgr.remove(name)) {
                        std::cout << "Workspace removed: " << p.positional[1] << "\n";
                    } else {
                        std::cerr << "Failed to remove workspace: " << p.positional[1] << "\n";
                        std::exit(1);
                    }
                } else if (subcmd == "restore") {
                    if (p.positional.size() < 2) {
                        std::cerr << "Usage: bengear workspace restore <name>\n";
                        std::exit(1);
                    }
                    auto name = container::String(std::move(p.positional[1]));
                    if (mgr.restore(name)) {
                        std::cout << "Workspace restored: " << p.positional[1] << "\n";
                    } else {
                        std::cerr << "Failed to restore workspace: " << p.positional[1] << "\n";
                        std::exit(1);
                    }
                } else {
                    std::cerr << "Unknown workspace subcommand: " << subcmd << "\n";
                    std::exit(1);
                }
                std::exit(0);
            })
            // session subcommand
            .command("session", "Session management", [&](const cli::Parsed& p) {
                ensure_loaded();
                auto ws_ctx = build_ws_ctx(config);

                if (p.positional.empty()) {
                    std::cerr << "Usage: bengear session <list|delete> [session_id]\n";
                    std::exit(1);
                }
                const auto& subcmd = p.positional[0];

                auto db_path = ws_ctx.tier_paths.user_dir / "history.db";
                ben_gear::workspace::HistoryDB db(db_path);

                if (subcmd == "list") {
                    auto ws_name = config.workspace_name.empty()
                        ? container::String("default") : config.workspace_name;
                    auto sessions = db.list_sessions(ws_name);
                    if (sessions.empty()) {
                        std::cout << "No sessions found.\n";
                    } else {
                        std::cout << "Sessions (" << sessions.size() << "):\n";
                        for (const auto& s : sessions) {
                            std::cout << "  " << s.dump(2) << "\n";
                        }
                    }
                } else if (subcmd == "delete") {
                    auto ws_name = config.workspace_name.empty()
                        ? container::String("default") : config.workspace_name;

                    // 解析选项：--all, --before, --after, --keyword, --confirm
                    bool opt_all = false;
                    std::string opt_before, opt_after, opt_keyword;
                    bool opt_confirm = false;
                    std::string sid_arg;

                    for (size_t i = 1; i < p.positional.size(); ++i) {
                        const auto& tok = p.positional[i];
                        if (tok == "--all") opt_all = true;
                        else if (tok == "--before" && i + 1 < p.positional.size()) opt_before = p.positional[++i];
                        else if (tok == "--after" && i + 1 < p.positional.size()) opt_after = p.positional[++i];
                        else if (tok == "--keyword" && i + 1 < p.positional.size()) opt_keyword = p.positional[++i];
                        else if (tok == "--confirm") opt_confirm = true;
                        else if (tok[0] != '-') sid_arg = tok;
                    }

                    // 交互式确认
                    auto ask_confirm = [](const std::string& desc) -> bool {
                        std::cout << desc << "\n确认删除？(y/N) ";
                        std::string input;
                        std::getline(std::cin, input);
                        return !input.empty() && (input[0] == 'y' || input[0] == 'Y');
                    };

                    if (opt_all) {
                        auto sessions = db.list_sessions(ws_name);
                        auto total = db.count_messages(ws_name);
                        if (opt_confirm || ask_confirm("将删除 " + std::to_string(sessions.size()) + " 个会话 (" + std::to_string(total) + " 条消息)")) {
                            int deleted = db.delete_all_sessions(ws_name);
                            std::cout << "Deleted " << deleted << " sessions.\n";
                        } else { std::cout << "Cancelled.\n"; }
                    } else if (!opt_before.empty()) {
                        auto ts = ben_gear::tools::parse_time_string(opt_before);
                        if (ts == 0) { std::cerr << "Invalid time: " << opt_before << "\n"; std::exit(1); }
                        auto sessions = db.list_sessions(ws_name);
                        int match = 0;
                        for (const auto& s : sessions) {
                            auto updated = s.value("updated_at", "");
                            if (updated.size() >= 10) {
                                auto s_ts = ben_gear::tools::parse_time_string(std::string(updated.data(), updated.size()).substr(0, 10));
                                if (s_ts > 0 && s_ts < ts) match++;
                            }
                        }
                        if (opt_confirm || ask_confirm("将删除 " + std::to_string(match) + " 个会话 (before " + opt_before + ")")) {
                            int deleted = db.delete_sessions_before(ws_name, ts);
                            std::cout << "Deleted " << deleted << " sessions.\n";
                        } else { std::cout << "Cancelled.\n"; }
                    } else if (!opt_after.empty()) {
                        auto ts = ben_gear::tools::parse_time_string(opt_after);
                        if (ts == 0) { std::cerr << "Invalid time: " << opt_after << "\n"; std::exit(1); }
                        auto sessions = db.list_sessions(ws_name);
                        int match = 0;
                        for (const auto& s : sessions) {
                            auto updated = s.value("updated_at", "");
                            if (updated.size() >= 10) {
                                auto s_ts = ben_gear::tools::parse_time_string(std::string(updated.data(), updated.size()).substr(0, 10));
                                if (s_ts > 0 && s_ts > ts) match++;
                            }
                        }
                        if (opt_confirm || ask_confirm("将删除 " + std::to_string(match) + " 个会话 (after " + opt_after + ")")) {
                            int deleted = db.delete_sessions_after(ws_name, ts);
                            std::cout << "Deleted " << deleted << " sessions.\n";
                        } else { std::cout << "Cancelled.\n"; }
                    } else if (!opt_keyword.empty()) {
                        auto results = db.search(container::String(opt_keyword.c_str()), ws_name, 1000);
                        std::set<std::string> ids;
                        for (const auto& r : results) {
                            if (r.contains("session_id")) ids.insert(r["session_id"].get<std::string>());
                        }
                        if (opt_confirm || ask_confirm("将删除 " + std::to_string(ids.size()) + " 个含 '" + opt_keyword + "' 的会话")) {
                            int deleted = db.delete_sessions_by_keyword(ws_name, container::String(opt_keyword.c_str()));
                            std::cout << "Deleted " << deleted << " sessions.\n";
                        } else { std::cout << "Cancelled.\n"; }
                    } else if (!sid_arg.empty()) {
                        auto sid = container::String(sid_arg.c_str());
                        if (db.delete_session(ws_name, sid)) {
                            std::cout << "Session deleted: " << sid_arg << "\n";
                        } else {
                            std::cerr << "Failed to delete session: " << sid_arg << "\n";
                            std::exit(1);
                        }
                    } else {
                        std::cerr << "Usage: bengear session delete <session_id> [--confirm]\n"
                                  << "       bengear session delete --all [--confirm]\n"
                                  << "       bengear session delete --before <date> [--confirm]\n"
                                  << "       bengear session delete --after <date> [--confirm]\n"
                                  << "       bengear session delete --keyword <kw> [--confirm]\n";
                        std::exit(1);
                    }
                } else {
                    std::cerr << "Unknown session subcommand: " << subcmd << "\n";
                    std::exit(1);
                }
                std::exit(0);
            })
            .command("serve", "Start HTTP/WebSocket server", [&](const cli::Parsed&) {
                ensure_loaded();
                ben_gear::log::configure(config);
                ben_gear::log::info_fmt("Starting server mode host={} port={}",
                    std::string(config.server.host.c_str()), config.server.port);
                ben_gear::server::Server srv(config);
                std::cout << "BenGear server listening on "
                          << std::string(config.server.host.c_str())
                          << ":" << config.server.port << std::endl;
                srv.run();
                std::exit(0);
            })
            .on_default([&](const cli::Parsed& p){ prompt_parts = std::move(p.positional); });

        parser.parse(argc, argv);
        ensure_loaded();

        // --new-session 清除 session_id 以强制创建新会话
        if (new_session) {
            config.session_id = container::String();
        }

        if (stream_override) {
            config.stream = stream_value;
        }
        ben_gear::log::configure(config);
        ben_gear::log::info_fmt("BenGear started provider={} model={} user={} workspace={}",
                                ben_gear::provider_name(config.provider), config.model,
                                std::string(config.username.empty() ? "default" : config.username.c_str()),
                                std::string(config.workspace_name.empty() ? "default" : config.workspace_name.c_str()));

        if (show_config) {
            print_config(config);
            return 0;
        }
        if (list_skills) {
            auto ws_ctx = build_ws_ctx(config);
            ben_gear::Agent agent(config, std::move(ws_ctx));
            auto& loader = agent.skill_loader();
            auto skills = loader.skills();
            if (skills.empty()) {
                std::cout << "No skills found.\n";
            } else {
                std::cout << "Skills (" << skills.size() << "):\n";
                for (const auto& [name, skill] : skills) {
                    std::cout << "  " << name;
                    if (!skill.version.empty()) std::cout << " v" << std::string(skill.version.c_str());
                    std::cout << " [" << std::string(skill.tier.c_str()) << "]";
                    if (!skill.enabled) std::cout << " (disabled)";
                    std::cout << "\n    " << std::string(skill.description.c_str()) << "\n";
                }
            }
            std::cout << "\nGlobal skills dir:  " << loader.global_dir().string() << "\n";
            std::cout << "Project skills dir: " << loader.project_dir().string() << "\n";
            return 0;
        }

        auto prompt = use_stdin ? ben_gear::read_all_stdin() : join_prompt(prompt_parts);
        if (prompt.empty()) {
            return run_chat(config, config.stream, async_mode, md_raw, !no_banner, new_session, no_thinking, no_tool, no_detail);
        }

        ben_gear::log::info_fmt("single request received stream={} async={}",
                                config.stream ? "true" : "false", async_mode ? "true" : "false");
        auto ws_ctx = build_ws_ctx(config);
        ben_gear::Agent agent(config, ws_ctx);

        // 始终创建 Session
        auto session = std::make_unique<ben_gear::workspace::Session>(
            ben_gear::workspace::SessionConfig{config.session_id, agent.settings().context_length, agent.settings().context_prune, ben_gear::agent::SessionType::main, {}},
            agent.resources()->make_session_deps(), agent.resources()->tools_mut());
        if (!config.session_id.empty()) {
            session->restore_from_db(agent.history_db());
        }

        auto& single_io_loop = agent.resources()->io_context()->loop();
         ben_gear::cli::DisplayConfig display_cfg;
         if (md_raw) display_cfg.markdown_render = false;
         if (no_thinking || no_detail) display_cfg.show_thinking = false;
         if (no_tool || no_detail) { display_cfg.show_tool_call = false; display_cfg.show_tool_result = false; }
         auto cli_app = ben_gear::cli::CliApp::create(display_cfg,
             std::string_view(config.model.data(), config.model.size()),
             config.context_length);
         cli_app->response_start();
 
         ben_gear::CancellationToken cancel;
         install_sigint_handler(cancel);
         auto prompt_str = ben_gear::base::container::String(std::move(prompt));
         auto result = ben_gear::net::sync_wait(single_io_loop, agent.run_session_async(single_io_loop, *session, std::move(prompt_str), cli_app->callbacks(), cancel));
         remove_sigint_handler();
         cli_app->response_end();
         if (result.status < 200 || result.status >= 300) {
             ben_gear::log::error_fmt("request failed status={}", result.status);
             std::cerr << "request failed with http status " << result.status << "\n" << result.raw << '\n';
            return 2;
        }
        std::cout << '\n';
        return 0;
    } catch (const std::exception& error) {
        ben_gear::log::error_fmt("fatal error: {}", error.what());
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
