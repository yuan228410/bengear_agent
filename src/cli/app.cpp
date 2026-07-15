#include "cli/app.hpp"

#include "ben_gear.hpp"
#include "base/log/configure.hpp"
#include "base/io/file.hpp"
#include "cli/args.hpp"
#include "cli/app_commands.hpp"
#include "cli/session_runner.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace ben_gear::cli {

int run_cli(int argc, char** argv) {
        namespace cli = ben_gear::cli;
        namespace container = ben_gear::base::container;
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
        bool exit_after_command = false;
        int exit_code = 0;
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
                  [&]{ parser.print_help(); exit_after_command = true; exit_code = 0; })
            .option('c', "config", "<path>", "JSON config file",
                    [&](std::string_view v){ model_config = v; })
            .option("active-model", "<name>", "Active model (name or provider:model)",
                    [&](std::string_view v){ active_model = v; })
            .option("workspace", "<path>", "Project workspace path",
                    [&](std::string_view v){ workspace = v; })
            // Multi-tier management options
            .option("user", "<name>", "Username (default: default)",
                    [&](std::string_view v){ ensure_loaded(); config.username = std::string(v.data()); })
            .option("workspace-name", "<name>", "Workspace name (default: default)",
                    [&](std::string_view v){ ensure_loaded(); config.workspace_name = std::string(v.data()); })
            .option("session", "<id>", "Resume session by ID",
                    [&](std::string_view v){ ensure_loaded(); config.session_id = std::string(v.data()); })
            .flag("new-session", "Force create a new session",
                  [&]{ new_session = true; })
            // Options below require config to be loaded
            .option("provider", "<name>", "openai|anthropic",
                    [&](std::string_view v){ ensure_loaded(); config.provider = ben_gear::parse_provider(v); })
            .option('m', "model", "<name>", "Model name",
                    [&](std::string_view v){ ensure_loaded(); config.model = std::string(v.data()); })
            .option("base-url", "<url>", "Base URL",
                    [&](std::string_view v){ ensure_loaded(); config.base_url = std::string(v.data()); })
            .option("api-url", "<url>", "API URL",
                    [&](std::string_view v){ ensure_loaded(); config.api_url = std::string(v.data()); })
            .option("api-key", "<key>", "API key",
                    [&](std::string_view v){ ensure_loaded(); config.api_key = std::string(v.data()); })
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
                exit_code = run_workspace_command(config, p);
                exit_after_command = true;
            })
            // session subcommand
            .command("session", "Session management", [&](const cli::Parsed& p) {
                ensure_loaded();
                exit_code = run_session_command(config, p);
                exit_after_command = true;
            })
            .command("serve", "Start HTTP/WebSocket server", [&](const cli::Parsed&) {
                ensure_loaded();
                exit_code = run_serve_command(config);
                exit_after_command = true;
            })
            .on_default([&](const cli::Parsed& p){ prompt_parts = std::move(p.positional); });

        parser.parse(argc, argv);
        if (exit_after_command) return exit_code;
        ensure_loaded();

        // --new-session 清除 session_id 以强制创建新会话
        if (new_session) {
            config.session_id = std::string();
        }

        if (stream_override) {
            config.stream = stream_value;
        }
        ben_gear::log::configure(config);
        ben_gear::log::info_fmt("BenGear started provider={} model={} user={} workspace={}",
                                ben_gear::provider_name(config.provider), config.model,
                                config.username.empty() ? "default" : config.username,
                                config.workspace_name.empty() ? "default" : config.workspace_name);

        if (show_config) {
            print_config(config);
            return 0;
        }
        if (list_skills) {
            return run_list_skills_command(config);
        }

        auto prompt = use_stdin ? ben_gear::read_all_stdin() : join_prompt(prompt_parts);
        SessionRunnerOptions session_options{md_raw, !no_banner, no_thinking, no_tool, no_detail};
        if (prompt.empty()) {
            return run_chat_session(config, session_options, new_session);
        }
        return run_single_request_session(config, std::move(prompt), session_options, async_mode);
}

}  // namespace ben_gear::cli
