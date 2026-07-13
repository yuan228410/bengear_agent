#include "cli/session_runner.hpp"

#include "ben_gear.hpp"
#include "base/net/cancel.hpp"
#include "cli/render/cli_app.hpp"
#include "cli/render/runtime_presenter.hpp"
#include "cli/repl/chat_repl.hpp"

#include <csignal>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

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

void update_trace_id(const ben_gear::workspace::WorkspaceContext& ws_ctx,
                      const ben_gear::workspace::Session& session) {
    std::string trace = std::string(ws_ctx.username.data(), ws_ctx.username.size()) + "-"
                 + std::string(ws_ctx.workspace_name.data(), ws_ctx.workspace_name.size()) + "-"
                 + std::string(session.session_id().data(), session.session_id().size());
    ben_gear::log::set_trace_id(std::move(trace));
}

}  // namespace

namespace ben_gear::cli {

int run_chat_session(const ben_gear::Config& config, const SessionRunnerOptions& options, bool force_new_session) {
    auto ws_ctx = build_ws_ctx(config);
    auto agent = std::make_shared<ben_gear::Agent>(config, ws_ctx);
    agent->post_init();

    // 交互模式：默认恢复最新会话，除非 force_new_session 或无历史会话
    auto session_id = config.session_id;
    if (session_id.empty() && !force_new_session) {
        auto sessions = agent->history_db().list_sessions(
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
        ben_gear::workspace::SessionConfig{session_id, agent->settings().context_length, agent->settings().context_prune, ben_gear::agent::SessionType::main, {}},
        agent->make_session_deps(), agent->tools_mut());
    if (!session_id.empty()) {
        session->restore_from_db(agent->history_db());
        ben_gear::log::info_fmt("session restored: id={}", std::string(session_id));
    }

    update_trace_id(ws_ctx, *session);

    ben_gear::cli::DisplayConfig display_cfg;
    if (options.markdown_raw) display_cfg.markdown_render = false;
    if (options.hide_thinking || options.hide_detail) display_cfg.show_thinking = false;
    if (options.hide_tool || options.hide_detail) { display_cfg.show_tool_call = false; display_cfg.show_tool_result = false; }
    auto cli_app = ben_gear::cli::CliApp::create(display_cfg,
        std::string_view(config.model.data(), config.model.size()),
        config.context_length);

    ben_gear::ChatRepl repl(*agent, *session, std::move(cli_app),
        ben_gear::ChatRepl::Config{"", true, options.show_banner, !session_id.empty()});

    int rc = repl.run();
    return rc;
}

int run_single_request_session(const ben_gear::Config& config, std::string prompt, const SessionRunnerOptions& options, bool async_mode) {
ben_gear::log::info_fmt("single request received stream={} async={}",
                        config.stream ? "true" : "false", async_mode ? "true" : "false");
auto ws_ctx = build_ws_ctx(config);
auto agent = std::make_shared<ben_gear::Agent>(config, ws_ctx);
agent->post_init();

// 始终创建 Session
auto session = std::make_unique<ben_gear::workspace::Session>(
    ben_gear::workspace::SessionConfig{config.session_id, agent->settings().context_length, agent->settings().context_prune, ben_gear::agent::SessionType::main, {}},
    agent->make_session_deps(), agent->tools_mut());
if (!config.session_id.empty()) {
    session->restore_from_db(agent->history_db());
}

auto& single_io_loop = agent->io_context()->loop();
 ben_gear::cli::DisplayConfig display_cfg;
 if (options.markdown_raw) display_cfg.markdown_render = false;
 if (options.hide_thinking || options.hide_detail) display_cfg.show_thinking = false;
 if (options.hide_tool || options.hide_detail) { display_cfg.show_tool_call = false; display_cfg.show_tool_result = false; }
 auto cli_app = ben_gear::cli::CliApp::create(display_cfg,
     std::string_view(config.model.data(), config.model.size()),
     config.context_length);
 cli_app->response_start();

 ben_gear::cli::RuntimePresenter runtime_presenter(std::cerr);
 ben_gear::application::CommandDescriptor descriptor;
 descriptor.action = ben_gear::base::container::String("cli.single_request");
 descriptor.username = config.username.empty() ? ben_gear::base::container::String("default") : config.username;
 descriptor.workspace_name = config.workspace_name.empty() ? ben_gear::base::container::String("default") : config.workspace_name;
 descriptor.session_id = session->session_id();
 descriptor.project_path = ben_gear::base::container::String(config.workspace.string().c_str());
 descriptor.subject = ben_gear::base::container::String("single request");
 descriptor.risk = ben_gear::application::CommandRisk::workspace_read;
 auto execution_request = ben_gear::application::command_execution_request(descriptor);
 ben_gear::application::RuntimeExecutionKernel runtime_kernel(ben_gear::application::RuntimeExecutionHooks{
     {},
     {},
     {},
     [&](const ben_gear::application::ExecutionRequest&, const ben_gear::application::ExecutionPlan&) {
         ben_gear::CancellationToken cancel;
         install_sigint_handler(cancel);
         auto prompt_str = ben_gear::base::container::String(std::move(prompt));
         auto result = ben_gear::net::sync_wait(single_io_loop, agent->run_session_async(single_io_loop, *session, std::move(prompt_str), cli_app->event_sink(), cancel));
         remove_sigint_handler();
         update_trace_id(ws_ctx, *session);
         if (result.status < 200 || result.status >= 300) {
             ben_gear::log::error_fmt("request failed status={}", result.status);
             return ben_gear::domain::AppResult<ben_gear::Json>::failure(
                 ben_gear::domain::AppError::internal(
                     ben_gear::base::container::String("llm_request_failed"),
                     ben_gear::base::container::String(result.raw.data(), result.raw.size())));
         }
         return ben_gear::domain::AppResult<ben_gear::Json>::success(
             ben_gear::Json{{"success", true}, {"http_status", result.status}});
     },
     {},
     [&](const ben_gear::core::RuntimeEvent& event) {
         runtime_presenter.on_event(event);
     }});
 auto execution = runtime_kernel.execute(execution_request);
 runtime_presenter.on_result(execution);
 cli_app->response_end();
 if (execution.status != ben_gear::application::ExecutionStatus::succeeded) {
     std::cerr << "request failed: " << execution.output.value("message", "execution failed") << '\n';
     return 2;
 }
std::cout << '\n';
return 0;
}


}  // namespace ben_gear::cli

