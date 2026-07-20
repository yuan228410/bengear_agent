#include "cli/session_runner.hpp"

#include "agent/runtime/exec_types.hpp"
#include "agent/runtime/runtime_factory.hpp"
#include "ben_gear.hpp"
#include "net/cancel.hpp"
#include "platform/platform.hpp"
#include "cli/render/cli_app.hpp"
#include "cli/repl/chat_repl.hpp"
#include "workspace/manager.hpp"
#include "workspace/history_db.hpp"

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace {

using namespace ben_gear;
namespace rt = ben_gear::agent::runtime;

// ─── CommandDescriptor → RuntimeBoundary 转换 ─────────────────────────

base::core::MutationScope command_mutation_scope(rt::CommandRisk risk) {
    switch (risk) {
    case rt::CommandRisk::read_only:        return base::core::MutationScope::none;
    case rt::CommandRisk::workspace_read:   return base::core::MutationScope::workspace_read;
    case rt::CommandRisk::workspace_write:  return base::core::MutationScope::workspace_write;
    case rt::CommandRisk::command_execution: return base::core::MutationScope::workspace_write;
    case rt::CommandRisk::destructive:      return base::core::MutationScope::repository_write;
    }
    return base::core::MutationScope::none;
}

base::core::RuntimeOperation to_runtime_operation(const rt::CommandDescriptor& command) {
    base::core::RuntimeOperation operation;
    operation.operation_id = command.action;
    operation.capability = base::core::RuntimeCapability::tool_call;
    operation.scope = command_mutation_scope(command.risk);
    operation.actor = command.username;
    operation.description = command.subject;
    operation.workspace.username = command.username;
    operation.workspace.workspace_name = command.workspace_name;
    operation.workspace.project_path = command.project_path;
    operation.workspace.session_id = command.session_id;
    return operation;
}

base::core::RuntimeBoundary command_runtime_boundary(const rt::CommandDescriptor& command) {
    base::core::RuntimeBoundary boundary;
    boundary.operation = to_runtime_operation(command);
    static_cast<void>(command);
    return boundary;
}

rt::ExecutionRequest command_execution_request(const rt::CommandDescriptor& command, bool dry_run = false) {
    rt::ExecutionRequest request;
    request.request_id = command.action;
    request.command = command;
    request.boundary = command_runtime_boundary(command);
    request.dry_run = dry_run;
    return request;
}

// ─── RuntimeExecutionKernel（仅本次使用）────────────────────────────────

struct RuntimeExecutionHooks {
    std::function<domain::AppResult<void>(const rt::ExecutionRequest&, const rt::ExecutionPlan&)> validate;
    std::function<domain::AppResult<void>(const rt::ExecutionRequest&, const rt::ExecutionPlan&)> authorize;
    std::function<domain::AppResult<void>(const rt::ExecutionRequest&, const rt::ExecutionPlan&)> checkpoint;
    std::function<domain::AppResult<Json>(const rt::ExecutionRequest&, const rt::ExecutionPlan&)> execute;
    std::function<void(const rt::ExecutionRequest&, const rt::ExecutionResult&)> audit;
    base::core::RuntimeEventSink event_sink;
};

static base::core::RuntimeStatus to_runtime_status(rt::ExecutionStatus st) {
    switch (st) {
    case rt::ExecutionStatus::planned:   return base::core::RuntimeStatus::planned;
    case rt::ExecutionStatus::running:   return base::core::RuntimeStatus::running;
    case rt::ExecutionStatus::succeeded: return base::core::RuntimeStatus::succeeded;
    case rt::ExecutionStatus::failed:    return base::core::RuntimeStatus::failed;
    case rt::ExecutionStatus::skipped:   return base::core::RuntimeStatus::skipped;
    }
    return base::core::RuntimeStatus::planned;
}

static base::core::RuntimeEvent make_event(const rt::ExecutionRequest& request, const rt::ExecutionPlan& plan,
                                     rt::ExecutionStepKind kind, base::core::RuntimeEventKind rk,
                                     rt::ExecutionStatus st, const std::string& msg = {}) {
    base::core::RuntimeEvent event;
    event.request_id = request.request_id;
    event.operation_id = plan.boundary.operation.operation_id;
    event.step_id = rt::to_string(kind);
    event.kind = rk;
    event.status = to_runtime_status(st);
    event.message = msg;
    return event;
}

class RuntimeExecutionKernel {
public:
    explicit RuntimeExecutionKernel(RuntimeExecutionHooks hooks = {}) : hooks_(std::move(hooks)) {}

    rt::ExecutionResult execute(const rt::ExecutionRequest& request) const {
        rt::ExecutionResult result;
        result.request_id = request.request_id;
        result.plan = make_execution_plan(request);
        result.status = request.dry_run ? rt::ExecutionStatus::planned : rt::ExecutionStatus::running;

        auto emit = [&](const base::core::RuntimeEvent& e) { if (hooks_.event_sink) hooks_.event_sink(e); };

        if (request.dry_run) {
            for (const auto& step : result.plan.steps) {
                rt::ExecutionTraceEvent ev;
                ev.step_id = step.step_id;
                ev.kind = step.kind;
                ev.status = rt::ExecutionStatus::planned;
                result.trace.push_back(ev);
                emit(make_event(request, result.plan, step.kind, base::core::RuntimeEventKind::step_skipped, rt::ExecutionStatus::planned, "dry run"));
            }
            result.output = Json{{"success", true}, {"dry_run", true}};
            return result;
        }

        auto fail = [&](rt::ExecutionStepKind kind, const domain::AppError& error) {
            emit(make_event(request, result.plan, kind, base::core::RuntimeEventKind::step_failed, rt::ExecutionStatus::failed, error.message));
            rt::ExecutionTraceEvent ev;
            ev.step_id = rt::to_string(kind);
            ev.kind = kind;
            ev.status = rt::ExecutionStatus::failed;
            ev.error_type = error.code;
            ev.message = error.message;
            result.trace.push_back(ev);
            result.status = rt::ExecutionStatus::failed;
            result.output = Json{{"success", false}, {"error_type", error.code}, {"message", error.message}};
            if (!error.details_json.empty()) result.output["details"] = error.details_json;
            if (hooks_.audit) hooks_.audit(request, result);
        };

        // validate
        if (hooks_.validate) {
            emit(make_event(request, result.plan, rt::ExecutionStepKind::validate, base::core::RuntimeEventKind::step_started, rt::ExecutionStatus::running));
            auto r = hooks_.validate(request, result.plan);
            if (!r.ok()) { fail(rt::ExecutionStepKind::validate, r.error()); return result; }
            rt::ExecutionTraceEvent ev;
            ev.step_id = rt::to_string(rt::ExecutionStepKind::validate);
            ev.kind = rt::ExecutionStepKind::validate;
            ev.status = rt::ExecutionStatus::succeeded;
            result.trace.push_back(ev);
            emit(make_event(request, result.plan, rt::ExecutionStepKind::validate, base::core::RuntimeEventKind::step_succeeded, rt::ExecutionStatus::succeeded));
        }

        // authorize
        if (hooks_.authorize) {
            emit(make_event(request, result.plan, rt::ExecutionStepKind::authorize, base::core::RuntimeEventKind::step_started, rt::ExecutionStatus::running));
            auto r = hooks_.authorize(request, result.plan);
            if (!r.ok()) { fail(rt::ExecutionStepKind::authorize, r.error()); return result; }
            rt::ExecutionTraceEvent ev;
            ev.step_id = rt::to_string(rt::ExecutionStepKind::authorize);
            ev.kind = rt::ExecutionStepKind::authorize;
            ev.status = rt::ExecutionStatus::succeeded;
            result.trace.push_back(ev);
            emit(make_event(request, result.plan, rt::ExecutionStepKind::authorize, base::core::RuntimeEventKind::step_succeeded, rt::ExecutionStatus::succeeded));
        }

        // checkpoint
        if (hooks_.checkpoint) {
            emit(make_event(request, result.plan, rt::ExecutionStepKind::checkpoint, base::core::RuntimeEventKind::step_started, rt::ExecutionStatus::running));
            auto r = hooks_.checkpoint(request, result.plan);
            if (!r.ok()) { fail(rt::ExecutionStepKind::checkpoint, r.error()); return result; }
            rt::ExecutionTraceEvent ev;
            ev.step_id = rt::to_string(rt::ExecutionStepKind::checkpoint);
            ev.kind = rt::ExecutionStepKind::checkpoint;
            ev.status = rt::ExecutionStatus::succeeded;
            result.trace.push_back(ev);
            emit(make_event(request, result.plan, rt::ExecutionStepKind::checkpoint, base::core::RuntimeEventKind::step_succeeded, rt::ExecutionStatus::succeeded));
        }

        // execute
        emit(make_event(request, result.plan, rt::ExecutionStepKind::execute, base::core::RuntimeEventKind::step_started, rt::ExecutionStatus::running));
        domain::AppResult<Json> exec_result = hooks_.execute
            ? hooks_.execute(request, result.plan)
            : domain::AppResult<Json>::success(Json{{"success", true}});
        if (!exec_result.ok()) { fail(rt::ExecutionStepKind::execute, exec_result.error()); return result; }
        result.output = exec_result.value();
        rt::ExecutionTraceEvent ev;
        ev.step_id = rt::to_string(rt::ExecutionStepKind::execute);
        ev.kind = rt::ExecutionStepKind::execute;
        ev.status = rt::ExecutionStatus::succeeded;
        ev.details = Json{{"output", result.output}};
        result.trace.push_back(ev);
        emit(make_event(request, result.plan, rt::ExecutionStepKind::execute, base::core::RuntimeEventKind::step_succeeded, rt::ExecutionStatus::succeeded));
        result.status = rt::ExecutionStatus::succeeded;

        // audit
        emit(make_event(request, result.plan, rt::ExecutionStepKind::audit, base::core::RuntimeEventKind::step_started, rt::ExecutionStatus::running));
        if (hooks_.audit) hooks_.audit(request, result);
        rt::ExecutionTraceEvent aev;
        aev.step_id = rt::to_string(rt::ExecutionStepKind::audit);
        aev.kind = rt::ExecutionStepKind::audit;
        aev.status = rt::ExecutionStatus::succeeded;
        result.trace.push_back(aev);
        emit(make_event(request, result.plan, rt::ExecutionStepKind::audit, base::core::RuntimeEventKind::step_succeeded, rt::ExecutionStatus::succeeded));

        return result;
    }

private:
    rt::ExecutionPlan make_execution_plan(const rt::ExecutionRequest& request) const {
        rt::ExecutionPlan plan;
        plan.plan_id = request.request_id.empty() ? request.command.action : request.request_id;
        plan.boundary = request.boundary;
        plan.dry_run = request.dry_run;
        return plan;
    }

    RuntimeExecutionHooks hooks_;
};

// ─── 工作空间名解析 ─────────────────────────────────────────────────

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

// ─── SIGINT 处理 ────────────────────────────────────────────────────

static std::atomic<bool> g_sigint_flag{false};

static void sigint_handler(int) {
    g_sigint_flag.store(true, std::memory_order_relaxed);
}

static void check_sigint(ben_gear::CancellationToken& token) {
    if (g_sigint_flag.exchange(false)) {
        token.cancel();
    }
}

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

} // namespace

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
    auto agent = ben_gear::agent::runtime::RuntimeFactory::create(config, ws_ctx);

    auto ws_name = resolve_ws_name(config);
    agent->services().resolve<workspace::WorkspaceManager>()->set_project_path(ws_name, config.workspace);

    auto session_id = config.session_id;
    if (session_id.empty() && !force_new_session) {
        auto sessions = agent->services().resolve<workspace::HistoryDB>()->list_sessions(
            std::string(ws_ctx.username), ws_name);
        if (!sessions.empty()) {
            auto& latest = sessions[0];
            if (latest.contains("session_id")) {
                session_id = latest["session_id"].get<std::string>();
                ben_gear::log::info_fmt("auto-resume latest session: id={}", std::string(session_id));
            }
        }
    }

    auto session = agent->make_session(session_id);
    update_trace_id(ws_ctx, *session);

    ben_gear::cli::DisplayConfig display_cfg;
    if (options.markdown_raw) display_cfg.markdown_render = false;
    if (options.hide_thinking || options.hide_detail) display_cfg.show_thinking = false;
    if (options.hide_tool || options.hide_detail) { display_cfg.show_tool_call = false; display_cfg.show_tool_result = false; }
    auto cli_app = ben_gear::cli::CliApp::create(display_cfg,
        std::string_view(config.llm.model.data(), config.llm.model.size()),
        config.llm.context_length);

    ben_gear::ChatRepl repl(*agent, *session, std::move(cli_app),
        ben_gear::ChatRepl::Config{"", true, options.show_banner, !session_id.empty()});

    return repl.run();
}

int run_single_request_session(const ben_gear::Config& config, std::string prompt, const SessionRunnerOptions& options, bool async_mode) {
    ben_gear::log::info_fmt("single request received stream={} async={}",
                            config.llm.stream ? "true" : "false", async_mode ? "true" : "false");
    auto ws_ctx = build_ws_ctx(config);
    auto agent = ben_gear::agent::runtime::RuntimeFactory::create(config, ws_ctx);

    auto ws_name = resolve_ws_name(config);
    agent->services().resolve<workspace::WorkspaceManager>()->set_project_path(ws_name, config.workspace);

    auto session = agent->make_session(config.session_id);

    auto& single_io_loop = agent->services().resolve<net::IoContext>()->loop();
    ben_gear::cli::DisplayConfig display_cfg;
    if (options.markdown_raw) display_cfg.markdown_render = false;
    if (options.hide_thinking || options.hide_detail) display_cfg.show_thinking = false;
    if (options.hide_tool || options.hide_detail) { display_cfg.show_tool_call = false; display_cfg.show_tool_result = false; }
    auto cli_app = ben_gear::cli::CliApp::create(display_cfg,
        std::string_view(config.llm.model.data(), config.llm.model.size()),
        config.llm.context_length);
    cli_app->response_start();

    auto& renderer = cli_app->renderer();

    // 将 Renderer 连接到 EventBus
    auto* event_bus = agent->services().resolve<base::EventBus>();
    if (event_bus) cli_app->connect_to_event_bus(*event_bus);

    rt::CommandDescriptor descriptor;
    descriptor.action = std::string("cli.single_request");
    descriptor.username = config.username.empty() ? std::string("default") : config.username;
    descriptor.workspace_name = resolve_ws_name(config);
    descriptor.session_id = session->session_id();
    descriptor.project_path = config.workspace.string();
    descriptor.subject = std::string("single request");
    descriptor.risk = rt::CommandRisk::workspace_read;

    auto execution_request = command_execution_request(descriptor);
    RuntimeExecutionKernel runtime_kernel(RuntimeExecutionHooks{
        {},
        {},
        {},
        [&](const rt::ExecutionRequest&, const rt::ExecutionPlan&) {
            SigintGuard sigint;
            auto prompt_str = std::string(std::move(prompt));
            auto result = ben_gear::net::sync_wait(single_io_loop, agent->run_session_async({single_io_loop, *session, std::move(prompt_str), sigint.token}));
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
        [&](const ben_gear::base::core::RuntimeEvent& event) {
            if (event.kind == ben_gear::base::core::RuntimeEventKind::state_changed) return;
            if (event.status == ben_gear::base::core::RuntimeStatus::failed) {
                renderer.on_error(event.message.empty() ? "request failed" : event.message);
            } else if (!event.message.empty()) {
                renderer.on_system(event.message);
            }
        }});
    auto execution = runtime_kernel.execute(execution_request);
    cli_app->response_end();
    if (execution.status != rt::ExecutionStatus::succeeded) {
        std::cerr << "request failed: " << execution.output.value("message", "execution failed") << '\n';
        return 2;
    }
    std::cout << '\n';
    return 0;
}

} // namespace ben_gear::cli
