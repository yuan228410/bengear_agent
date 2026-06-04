#include "ben_gear/ben_gear.hpp"
#include "ben_gear/cli/args.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

class TerminalAgentCallbacks final : public ben_gear::AgentCallbacks {
public:
    void on_thinking(std::string_view token) override {
        if (!thinking_started_) {
            std::cerr << "\n[thinking] " << std::flush;
            thinking_started_ = true;
        }
        std::cerr << token << std::flush;
    }

    void on_token(std::string_view token) override {
        if (thinking_started_) {
            std::cerr << "\n[/thinking]\n\n" << std::flush;
            thinking_started_ = false;
        }
        std::cout << token << std::flush;
    }

    void on_tool_call(const ben_gear::ToolCallRequest& call) override {
        if (thinking_started_) {
            std::cerr << "\n[/thinking]\n\n";
            thinking_started_ = false;
        }
        std::cerr << "\n[tool call] " << std::string(call.name.c_str());
        std::cerr << " id=" << std::string(call.id.c_str());
        std::cerr << " args=" << call.arguments.dump();
        std::cerr << '\n';
    }

    void on_tool_result(const ben_gear::ToolCallResult& result) override {
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
    bool thinking_started_ = false;
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
              << "connection_pool.enable_keep_alive=" << (config.connection_pool.enable_keep_alive ? "true" : "false") << '\n'
              << "thread_pool.min_threads=" << config.thread_pool.min_threads << '\n'
              << "thread_pool.max_threads=" << config.thread_pool.max_threads << '\n'
              << "thread_pool.max_queue_size=" << config.thread_pool.max_queue_size << '\n'
              << "mcp.read_buffer_size=" << config.mcp.read_buffer_size << '\n'
              << "mcp_servers=" << config.mcp_servers.size() << '\n'
              << "anthropic_api_version=" << (config.anthropic_api_version.empty() ? "<default>" : std::string(config.anthropic_api_version.c_str())) << '\n';
}

ben_gear::llm::StreamResult run_agent_stream_async(ben_gear::net::EventLoop& loop,
                                                    ben_gear::Agent& agent,
                                                    std::string prompt,
                                                    TerminalAgentCallbacks& callbacks) {
    return loop.run(agent.run_stream_async(loop, std::move(prompt), nullptr, callbacks));
}

ben_gear::llm::ChatResult run_agent_async(ben_gear::net::EventLoop& loop,
                                          ben_gear::Agent& agent,
                                          std::string prompt,
                                          TerminalAgentCallbacks& callbacks) {
    auto result = loop.run(agent.run_async(loop, std::move(prompt), callbacks));
    if (result.status >= 200 && result.status < 300) {
        callbacks.on_token(result.text);
    }
    return result;
}

int run_chat(const ben_gear::Config& config, bool stream, bool async_mode) {
    ben_gear::Agent agent(config);
    ben_gear::net::NetworkRuntime runtime;
    ben_gear::net::EventLoop loop;
    TerminalAgentCallbacks callbacks;
    std::cout << "BenGear chat started. Type /exit to quit.\n";
    for (;;) {
        std::cout << "> " << std::flush;
        std::string line;
        if (!std::getline(std::cin, line)) break;
        if (line == "/exit" || line == "/quit") break;
        if (ben_gear::base::utils::trim(line).empty()) continue;
        ben_gear::log::info_fmt("chat request received stream={} async={}",
                                stream ? "true" : "false", async_mode ? "true" : "false");
        if (stream) {
            auto result = async_mode ? run_agent_stream_async(loop, agent, line, callbacks) : agent.run_stream(line, nullptr, callbacks);
            if (result.status < 200 || result.status >= 300) {
                ben_gear::log::error_fmt("request failed status={}", result.status);
                std::cerr << "request failed with http status " << result.status << "\n" << result.raw << '\n';
                continue;
            }
            std::cout << "\n";
        } else {
            auto result = async_mode ? run_agent_async(loop, agent, line, callbacks) : agent.run(line, callbacks);
            if (result.status < 200 || result.status >= 300) {
                ben_gear::log::error_fmt("request failed status={}", result.status);
                std::cerr << "request failed with http status " << result.status << "\n" << result.raw << '\n';
                continue;
            }
            std::cout << result.text << "\n";
        }
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        namespace cli = ben_gear::cli;
        namespace container = ben_gear::base::container;

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
                "  bengear [options] --chat")
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
            .option("workspace", "<path>", "Workspace path",
                    [&](std::string_view v){ workspace = v; })
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
            .on_default([&](const cli::Parsed& p){ prompt_parts = std::move(p.positional); });

        parser.parse(argc, argv);
        ensure_loaded();
        const bool stream = stream_override ? stream_value : config.stream;
        ben_gear::log::configure(config);
        ben_gear::log::info_fmt("BenGear started provider={} model={}",
                                ben_gear::provider_name(config.provider), config.model);

        if (show_config) {
            print_config(config);
            return 0;
        }
        if (list_skills) {
            ben_gear::Agent agent(std::move(config));
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
            return run_chat(config, stream, async_mode);
        }

        auto prompt = use_stdin ? ben_gear::read_all_stdin() : join_prompt(prompt_parts);
        if (prompt.empty()) {
            return run_chat(config, stream, async_mode);
        }

        ben_gear::log::info_fmt("single request received stream={} async={}",
                                stream ? "true" : "false", async_mode ? "true" : "false");
        ben_gear::Agent agent(std::move(config));
        ben_gear::net::NetworkRuntime runtime;
        ben_gear::net::EventLoop loop;
        TerminalAgentCallbacks callbacks;
        if (stream) {
            auto result = async_mode ? run_agent_stream_async(loop, agent, std::move(prompt), callbacks) : agent.run_stream(std::move(prompt), nullptr, callbacks);
            if (result.status < 200 || result.status >= 300) {
                ben_gear::log::error_fmt("request failed status={}", result.status);
                std::cerr << "request failed with http status " << result.status << "\n" << result.raw << '\n';
                return 2;
            }
            std::cout << '\n';
        } else {
            auto result = async_mode ? run_agent_async(loop, agent, std::move(prompt), callbacks) : agent.run(std::move(prompt), callbacks);
            if (result.status < 200 || result.status >= 300) {
                ben_gear::log::error_fmt("request failed status={}", result.status);
                std::cerr << "request failed with http status " << result.status << "\n" << result.raw << '\n';
                return 2;
            }
            std::cout << result.text << '\n';
        }
        return 0;
    } catch (const std::exception& error) {
        ben_gear::log::error_fmt("fatal error: {}", error.what());
        std::cerr << "error: " << error.what() << '\n';
        return 1;
    }
}
