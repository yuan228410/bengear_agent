#include "cli/app_commands.hpp"
#include "cli/session_runner.hpp"

#include "ben_gear.hpp"
#include "base/log/configure.hpp"
#include "server/core/server.hpp"
#include "tool/history_tools.hpp"

#include <cstdlib>
#include <iostream>
#include <set>
#include <string>
#include <utility>

namespace ben_gear::cli {

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

int run_workspace_command(const Config& config, const Parsed& p) {
    namespace container = ben_gear::base::container;
    namespace ws = ben_gear::workspace;
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
return 0;
}

int run_session_command(const Config& config, const Parsed& p) {
    namespace container = ben_gear::base::container;
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
return 0;
}

int run_serve_command(const Config& config) {
ben_gear::log::configure(config);
ben_gear::log::info_fmt("Starting server mode host={} port={}",
    std::string(config.server.host.c_str()), config.server.port);
ben_gear::server::Server srv(config);
std::cout << "BenGear server listening on "
          << std::string(config.server.host.c_str())
          << ":" << config.server.port << std::endl;
srv.run();
return 0;
}

int run_list_skills_command(const Config& config) {
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

}  // namespace ben_gear::cli
