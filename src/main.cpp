#include "ben_gear/ben_gear.hpp"
#include "ben_gear/cli/args.hpp"
#include "ben_gear/base/net/cancel.hpp"

#include <csignal>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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

class TerminalAgentCallbacks final : public ben_gear::AgentCallbacks {
public:
    void on_thinking(std::string_view token) const override {
        if (!thinking_started_) {
            std::cerr << "\n[thinking] " << std::flush;
            thinking_started_ = true;
        }
        std::cerr << token << std::flush;
    }

    void on_token(std::string_view token) const override {
        if (token.empty()) return;
        if (thinking_started_) {
            std::cerr << "\n[/thinking]\n\n" << std::flush;
            thinking_started_ = false;
        }
        std::cout << token << std::flush;
    }

    void on_tool_call(const ben_gear::ToolCallRequest& call) const override {
        if (thinking_started_) {
            std::cerr << "\n[/thinking]\n\n" << std::flush;
            thinking_started_ = false;
        }
        std::cerr << "\n[tool call] " << std::string(call.name.c_str());
        std::cerr << " id=" << std::string(call.id.c_str());
        std::cerr << " args=" << call.arguments.dump();
        std::cerr << '\n';
    }

    void on_tool_result(const ben_gear::ToolCallResult& result) const override {
        std::cerr << "[tool result] " << (result.success ? "ok" : "error");
        std::cerr << " id=" << std::string(result.tool_call_id.c_str());
        std::cerr << " bytes=" << result.output.size() << '\n';
    }

    ~TerminalAgentCallbacks() {
        if (thinking_started_) {
            std::cerr << "\n[/thinking]\n";
        }
    }

private:
    mutable bool thinking_started_ = false;
};

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
              << "role=" << (config.role.empty() ? "lead" : std::string(config.role.c_str())) << '\n'
              << "session_id=" << (config.session_id.empty() ? "<new>" : std::string(config.session_id.c_str())) << '\n';
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
        username,
        config.session_id
    };
}

int run_chat(const ben_gear::Config& config, bool stream, bool async_mode) {
    auto ws_ctx = build_ws_ctx(config);
    ben_gear::Agent agent(config, ws_ctx);

    // 始终创建 Session
    auto session = std::make_unique<ben_gear::workspace::Session>(
        ben_gear::workspace::SessionConfig{config.session_id, agent.settings().context_length},
        agent.resources()->make_session_deps());
    if (!config.session_id.empty()) {
        session->restore_from_db(agent.history_db());
        ben_gear::log::info_fmt("session restored: id={}",
                                std::string(config.session_id.data(), config.session_id.size()));
    }

    ben_gear::net::NetworkRuntime runtime;
    ben_gear::net::EventLoop loop;
    TerminalAgentCallbacks callbacks;
    std::cout << "BenGear chat started. Type /exit to quit.\n";
    for (;;) {
        std::cout << "> " << std::flush;
        std::string line;
        if (!std::getline(std::cin, line)) {
            ben_gear::log::info_fmt("chat: stdin EOF or error, exiting");
            break;
        }
        if (line == "/exit" || line == "/quit") break;
        if (ben_gear::base::utils::trim(line).empty()) continue;
        ben_gear::log::info_fmt("chat request received stream={} async={}",
                                stream ? "true" : "false", async_mode ? "true" : "false");

        ben_gear::CancellationToken cancel;
        install_sigint_handler(cancel);
        try {
            auto prompt = ben_gear::base::container::String(line.c_str());
            auto result = loop.run(agent.run_session_async(loop, *session, std::move(prompt), callbacks, cancel), cancel);
            if (result.status < 200 || result.status >= 300) {
                ben_gear::log::error_fmt("request failed status={}", result.status);
                std::cerr << "request failed with http status " << result.status << "\n" << result.raw << '\n';
                continue;
            }
            std::cout << "\n";
        } catch (const ben_gear::OperationCancelled&) {
            std::cerr << "\n[cancelled]\n";
        } catch (const std::exception& e) {
            ben_gear::log::error_fmt("chat error: {}", e.what());
            std::cerr << "error: " << e.what() << "\n";
        }
        remove_sigint_handler();
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        namespace cli = ben_gear::cli;
        namespace container = ben_gear::base::container;
        namespace ws = ben_gear::workspace;

        std::filesystem::path workspace = std::filesystem::current_path();
        std::filesystem::path model_config;
        std::string active_model;
        bool use_stdin = false;
        bool show_config = false;
        bool chat = false;
        bool stream_override = false;
        bool stream_value = true;
        bool async_mode = false;
        bool list_skills = false;
        bool new_session = false;
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
                "  bengear [options] --chat\n"
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
            .option("role", "<name>", "Agent role (default: lead)",
                    [&](std::string_view v){ ensure_loaded(); config.role = container::String(v.data()); })
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
            .flag("chat", "Interactive chat mode", [&]{ ensure_loaded(); chat = true; })
            .flag("stdin", "Read prompt from stdin", [&]{ use_stdin = true; })
            .flag('s', "stream", "Enable streaming", [&]{ stream_value = true; stream_override = true; })
            .flag("no-stream", "Disable streaming", [&]{ stream_value = false; stream_override = true; })
            .flag('a', "async", "Use async mode", [&]{ async_mode = true; })
            .flag("sync", "Use sync mode", [&]{ async_mode = false; })
            .flag("show-config", "Print config and exit", [&]{ ensure_loaded(); show_config = true; })
            .flag("list-skills", "List skills and exit", [&]{ ensure_loaded(); list_skills = true; })
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
                    auto name = container::String(p.positional[1].c_str());
                    container::String project_path;
                    if (p.positional.size() >= 3) {
                        project_path = container::String(p.positional[2].c_str());
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
                    auto name = container::String(p.positional[1].c_str());
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
                    auto name = container::String(p.positional[1].c_str());
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
                ben_gear::session::HistoryDB db(db_path);

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
                    if (p.positional.size() < 2) {
                        std::cerr << "Usage: bengear session delete <session_id>\n";
                        std::exit(1);
                    }
                    auto ws_name = config.workspace_name.empty()
                        ? container::String("default") : config.workspace_name;
                    auto sid = container::String(p.positional[1].c_str());
                    if (db.delete_session(ws_name, sid)) {
                        std::cout << "Session deleted: " << p.positional[1] << "\n";
                    } else {
                        std::cerr << "Failed to delete session: " << p.positional[1] << "\n";
                        std::exit(1);
                    }
                } else {
                    std::cerr << "Unknown session subcommand: " << subcmd << "\n";
                    std::exit(1);
                }
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
        ben_gear::log::info_fmt("BenGear started provider={} model={} user={} workspace={} role={}",
                                ben_gear::provider_name(config.provider), config.model,
                                std::string(config.username.empty() ? "default" : config.username.c_str()),
                                std::string(config.workspace_name.empty() ? "default" : config.workspace_name.c_str()),
                                std::string(config.role.empty() ? "lead" : config.role.c_str()));

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
        if (chat) {
            return run_chat(config, config.stream, async_mode);
        }

        auto prompt = use_stdin ? ben_gear::read_all_stdin() : join_prompt(prompt_parts);
        if (prompt.empty()) {
            return run_chat(config, config.stream, async_mode);
        }

        ben_gear::log::info_fmt("single request received stream={} async={}",
                                config.stream ? "true" : "false", async_mode ? "true" : "false");
        auto ws_ctx = build_ws_ctx(config);
        ben_gear::Agent agent(config, ws_ctx);

        // 始终创建 Session
        auto session = std::make_unique<ben_gear::workspace::Session>(
            ben_gear::workspace::SessionConfig{config.session_id, agent.settings().context_length},
            agent.resources()->make_session_deps());
        if (!config.session_id.empty()) {
            session->restore_from_db(agent.history_db());
        }

        ben_gear::net::NetworkRuntime runtime;
        ben_gear::net::EventLoop loop;
        TerminalAgentCallbacks callbacks;

        ben_gear::CancellationToken cancel;
        install_sigint_handler(cancel);
        auto prompt_str = ben_gear::base::container::String(prompt.c_str());
        auto result = loop.run(agent.run_session_async(loop, *session, std::move(prompt_str), callbacks, cancel), cancel);
        remove_sigint_handler();
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
