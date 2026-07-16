#include "cli/session_runner.hpp"

#include "agent/runtime/application/command_governance.hpp"
#include "ben_gear.hpp"
#include "base/net/cancel.hpp"
#include "cli/render/cli_app.hpp"
#include "cli/render/runtime_presenter.hpp"
#include "cli/repl/chat_repl.hpp"

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace {

/// 解析工作空间名
/// 格式: <目录名>_<6位hex路径哈希>，如 bengear_agent_a3f2c1
/// 避免同名目录冲突：/home/a/foo 和 /work/foo → foo_xxxx 和 foo_yyyy
std::string resolve_ws_name(const ben_gear::Config& config) {
    if (!config.workspace_name.empty()) return config.workspace_name;

    auto path = config.workspace.string();
    if (path.empty() || path == "/" || path == ".") {
        return std::string("default");
    }

    uint32_t h = 0;
    for (char c : path) h = h * 31 + static_cast<unsigned char>(c);
    char suffix[8];
    snprintf(suffix, sizeof(suffix), "_%06x", h & 0xFFFFFF);

    auto dirname = config.workspace.filename().string();
    for (auto& c : dirname) {
        if (c == '/' || c == '\\' || c == '.' || c == ':' || c == '\0') c = '_';
    }
    return (dirname + suffix);
}

/// SIGINT 处理
/// 信号处理函数只设置 atomic flag（async-signal-safe），不直接操作对象指针
/// 安全上下文中通过 check_sigint() 将 flag 转发给 CancellationToken
static std::atomic<bool> g_sigint_flag{false};

static void sigint_handler(int) {
    g_sigint_flag.store(true, std::memory_order_relaxed);
}

/// 在安全上下文（非信号处理函数）中检查并执行取消
static void check_sigint(ben_gear::CancellationToken& token) {
    if (g_sigint_flag.exchange(false)) {
        token.cancel();
    }
}

/// 安装信号处理 + 启动轮询线程（保证同步等待期间也能响应 Ctrl+C）
struct SigintGuard {
    ben_gear::CancellationToken token;
    std::thread poller;
    std::atomic<bool> stop{false};

    SigintGuard() {
        std::signal(SIGINT, sigint_handler);
        poller = std::thread([this] {
            while (!stop.load(std::memory_order_relaxed)) {
                check_sigint(token);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
    }

    ~SigintGuard() {
        stop.store(true, std::memory_order_relaxed);
        if (poller.joinable()) poller.join();
        std::signal(SIGINT, SIG_DFL);
    }
};


void update_trace_id(const ben_gear::workspace::WorkspaceContext& ws_ctx,
                      const ben_gear::workspace::Session& session) {
    std::string trace = std::string(ws_ctx.username.data(), ws_ctx.username.size()) + "-"
                 + std::string(ws_ctx.workspace_name.data(), ws_ctx.workspace_name.size()) + "-"
                 + std::string(session.session_id().data(), session.session_id().size());
    ben_gear::log::set_trace_id(std::move(trace));
}

}  // namespace

namespace ben_gear::cli {

ben_gear::workspace::WorkspaceContext build_ws_ctx(const ben_gear::Config& config) {
    namespace ws = ben_gear::workspace;
    namespace container = ben_gear::base::container;

    auto root = ben_gear::support::data_directory();
    auto username = config.username.empty() ? std::string("default") : config.username;
    auto ws_name = resolve_ws_name(config);

    ws::TierPaths tier_paths{
        root,
        root / "users" / std::string(username.data(), username.size()),
        root / "users" / std::string(username.data(), username.size())
             / "workspaces" / std::string(ws_name.data(), ws_name.size())
    };

    return ws::WorkspaceContext{
        std::move(tier_paths),
        ws_name,
        config.workspace.string(),
        username,
        config.session_id
    };
}

int run_chat_session(const ben_gear::Config& config, const SessionRunnerOptions& options, bool force_new_session) {
    auto ws_ctx = build_ws_ctx(config);
    auto agent = std::make_shared<ben_gear::Agent>(config, ws_ctx);
    agent->post_init();

    // 记录当前工作空间的项目路径
    auto ws_name = resolve_ws_name(config);
    agent->workspace_manager()->set_project_path(ws_name, config.workspace);

    // 交互模式：默认恢复最新会话，除非 force_new_session 或无历史会话
    auto session_id = config.session_id;
    if (session_id.empty() && !force_new_session) {
        auto sessions = agent->history_db().list_sessions(
            ws_name);
        if (!sessions.empty()) {
            auto& latest = sessions[0];
            if (latest.contains("session_id")) {
                session_id = latest["session_id"].get<std::string>();
                ben_gear::log::info_fmt("auto-resume latest session: id={}", std::string(session_id));
            }
        }
    }

    // 创建 Session（可能恢复历史）
    auto session = agent->make_session(session_id);

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

auto ws_name = resolve_ws_name(config);
agent->workspace_manager()->set_project_path(ws_name, config.workspace);

auto session = agent->make_session(config.session_id);

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
 descriptor.action = std::string("cli.single_request");
 descriptor.username = config.username.empty() ? std::string("default") : config.username;
  descriptor.workspace_name = resolve_ws_name(config);
 descriptor.session_id = session->session_id();
 descriptor.project_path = config.workspace.string();
 descriptor.subject = std::string("single request");
 descriptor.risk = ben_gear::application::CommandRisk::workspace_read;
 auto execution_request = ben_gear::application::command_execution_request(descriptor);
 ben_gear::application::RuntimeExecutionKernel runtime_kernel(ben_gear::application::RuntimeExecutionHooks{
     {},
     {},
     {},
      [&](const ben_gear::application::ExecutionRequest&, const ben_gear::application::ExecutionPlan&) {
          SigintGuard sigint;
          auto prompt_str = std::string(std::move(prompt));
          auto result = ben_gear::net::sync_wait(single_io_loop, agent->run_session_async({single_io_loop, *session, std::move(prompt_str), cli_app->sinks(), sigint.token}));
         update_trace_id(ws_ctx, *session);
         if (result.status < 200 || result.status >= 300) {
             ben_gear::log::error_fmt("request failed status={}", result.status);
             return ben_gear::domain::AppResult<ben_gear::Json>::failure(
                 ben_gear::domain::AppError::internal(
                     std::string("llm_request_failed"),
                     std::string(result.raw.data(), result.raw.size())));
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

