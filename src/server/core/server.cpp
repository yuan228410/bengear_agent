#include "ben_gear/server/core/server.hpp"
#include "ben_gear/server/ws/protocol.hpp"
#include "ben_gear/server/http/parser.hpp"
#include "ben_gear/server/auth/auth.hpp"
#include "ben_gear/server/api/handlers.hpp"
#include "ben_gear/server/api/file_api.hpp"
#include "ben_gear/base/log/logger.hpp"
#include "ben_gear/base/net/cancel.hpp"
#include "ben_gear/base/tier_paths.hpp"
#include "ben_gear/llm/run_outcome.hpp"
#include "ben_gear/base/platform/platform.hpp"
#include "ben_gear/workspace/uuid.hpp"
#include "ben_gear/workspace/manager.hpp"

#include <chrono>
#include <map>
#include <memory>
#include <string>

namespace ben_gear::server {

Server::Server(config::Settings settings)
    : settings_(std::move(settings)),
      router_(std::make_unique<Router>()),
      session_pool_(std::make_unique<SessionPool>(settings_.server.agent_pool_max_size)),
      static_files_(std::make_unique<StaticFileServer>(std::string(settings_.server.static_dir.c_str()))),
      io_context_(std::make_shared<net::IoContext>("server")) {
    setup_routes();
    log::info_fmt("Server: initialized on {}:{}", settings_.server.host.c_str(), settings_.server.port);
}

Server::~Server() { stop(); }

std::filesystem::path Server::user_dir_for(const container::String& username) const {
    return ben_gear::support::data_directory() / "users" / std::string(username.c_str());
}

workspace::TierPaths Server::tier_paths_for(const container::String& username,
                                             const container::String& workspace) const {
    auto root = ben_gear::support::data_directory();
    auto user_dir = root / "users" / std::string(username.c_str());
    auto ws_dir = user_dir / "workspaces" / std::string(workspace.c_str());
    return workspace::TierPaths{root, user_dir, ws_dir};
}

container::String Server::project_path_for(const container::String& username,
                                           const container::String& workspace) const {
    workspace::WorkspaceManager mgr(user_dir_for(username));
    auto meta = mgr.get(workspace);
    if (meta && !meta->project_path.empty()) return meta->project_path;
    return container::String(settings_.workspace.string().c_str());
}

void Server::setup_routes() {
    auto default_ws = std::string(settings_.workspace_name.c_str());

    // SessionService — 所有回调从 req.username 获取当前用户，按用户隔离
    SessionService session_svc;
    session_svc.get_user_dir = [this](const container::String& username) {
        return user_dir_for(username);
    };
    session_svc.list_sessions = [this](const container::String& workspace, const container::String& username) {
        auto ws = workspace.empty() ? container::String(settings_.workspace_name.c_str()) : workspace;
        auto db_path = user_dir_for(username) / "history.db";
        workspace::HistoryDB db(db_path);
        return db.list_sessions(ws);
    };
    session_svc.list_sessions_by_workspace = [this](const container::String& ws_name, const container::String& username) {
        auto db_path = user_dir_for(username) / "history.db";
        workspace::HistoryDB db(db_path);
        return db.list_sessions(ws_name);
    };
    session_svc.create_session = [this](const container::String& name, const container::String& ws_name, const container::String& username) {
        auto sid = std::string(workspace::generate_uuid().c_str());
        auto ws = ws_name.empty() ? container::String(settings_.workspace_name.c_str()) : ws_name;
        auto project_path = project_path_for(username, ws);
        auto ws_ctx = workspace::WorkspaceContext{
            tier_paths_for(username, ws),
            ws, project_path, username, container::String(sid.c_str())};
        log::info_fmt("Server: create_session user={} workspace={} session={} project_path={}",
                      username.c_str(), ws.c_str(), sid.c_str(), project_path.c_str());
        auto entry = session_pool_->get_or_create(
            container::String(sid.c_str()), username, ws, settings_, ws_ctx);
        entry->agent->history_db().create_session(ws, container::String(sid.c_str()), name);
        return container::String(sid.c_str());
    };
    session_svc.delete_session = [this](const container::String& sid, const container::String& workspace, const container::String& username) {
        auto ws = workspace.empty() ? container::String(settings_.workspace_name.c_str()) : workspace;
        auto db_path = user_dir_for(username) / "history.db";
        log::info_fmt("API delete_session: sid={} ws={} user={} db_path={}",
                      sid.c_str(), ws.c_str(), username.c_str(), db_path.string().c_str());
        workspace::HistoryDB db(db_path);
        auto ok = db.delete_session(ws, sid);
        if (ok) {
            log::info_fmt("API delete_session: DB delete OK");
        } else {
            log::error_fmt("API delete_session: DB delete FAILED");
        }
        session_pool_->remove(sid, username, ws);
        return ok;
    };
    session_svc.rename_session = [this](const container::String& sid, const container::String& name, const container::String& workspace, const container::String& username) {
        auto ws = workspace.empty() ? container::String(settings_.workspace_name.c_str()) : workspace;
        if (auto entry = session_pool_->get(sid, username, ws)) {
            return entry->agent->history_db().rename_session(ws, sid, name);
        }
        auto db_path = user_dir_for(username) / "history.db";
        workspace::HistoryDB db(db_path);
        return db.rename_session(ws, sid, name);
    };
    session_svc.load_history = [this](const container::String& sid, const container::String& ws_name, int limit, const container::String& username) {
        container::String ws = ws_name.empty() ? container::String(settings_.workspace_name.c_str()) : ws_name;
        // 按用户加载历史：每个用户有自己的 history.db
        auto db_path = user_dir_for(username) / "history.db";
        workspace::HistoryDB db(db_path);
        return db.load_session(ws, sid, limit);
    };

    // WorkspaceService — 每个用户独立的工作空间列表
    // 用闭包 + req.username 动态构建 WorkspaceManager
    WorkspaceService ws_svc;
    ws_svc.list_workspaces = [this](const container::String& username) {
        auto user_dir = user_dir_for(username);
        workspace::WorkspaceManager mgr(user_dir);
        container::Vector<WorkspaceInfo> result;
        auto metas = mgr.list_all();
        for (const auto& m : metas) {
            WorkspaceInfo info;
            info.name = m.name;
            info.path = std::string(m.project_path.data(), m.project_path.size());
            result.push_back(std::move(info));
        }
        return result;
    };
    ws_svc.create_workspace = [this](const container::String& name, const container::String& project_path, const container::String& username) {
        auto user_dir = user_dir_for(username);
        workspace::WorkspaceManager mgr(user_dir);
        auto meta = mgr.create(name, project_path);
        if (!meta) return std::optional<WorkspaceInfo>();
        WorkspaceInfo info;
        info.name = meta->name;
        info.path = std::string(meta->project_path.data(), meta->project_path.size());
        return std::optional<WorkspaceInfo>(std::move(info));
    };
    ws_svc.delete_workspace = [this](const container::String& name, const container::String& username) {
        auto user_dir = user_dir_for(username);
        workspace::WorkspaceManager mgr(user_dir);
        return mgr.remove(name);
    };

    ConfigService config_svc;
    config_svc.get_config = [this, default_ws]() {
        ConfigInfo info;
        info.model = settings_.model;
        info.provider = provider_name(settings_.provider);
        info.workspace = container::String(default_ws.c_str());
        info.display_name = settings_.display_name;
        info.version = container::String(BEN_GEAR_VERSION);
        return info;
    };
    config_svc.set_model = [this](const container::String& model) {
        settings_.model = model;
    };

    McpService mcp_svc;
    mcp_svc.get_status = []() { return std::string(R"({"servers":[]})"); };

    // FileService — 浏览服务器文件系统
    FileService file_svc;
    file_svc.home_directory = [](const container::String& /*username*/) {
        return container::String(ben_gear::support::home_directory().string().c_str());
    };
    file_svc.list_files = [](const container::String& path, const container::String& /*username*/) {
        container::Vector<FileEntry> entries;
        auto dir_path = std::string(path.empty() ? "/" : path.c_str());
        try {
            if (!std::filesystem::exists(dir_path)) return entries;
            for (const auto& entry : std::filesystem::directory_iterator(dir_path)) {
                FileEntry fe;
                fe.name = container::String(entry.path().filename().string().c_str());
                fe.type = container::String(entry.is_directory() ? "dir" : "file");
                if (entry.is_regular_file()) fe.size = entry.file_size();
                auto ft = entry.last_write_time();
                auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                    ft - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
                auto tt = std::chrono::system_clock::to_time_t(sctp);
                std::tm tm;
                ::localtime_r(&tt, &tm);
                std::ostringstream oss;
                oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
                fe.modified = container::String(oss.str().c_str());
                entries.push_back(std::move(fe));
            }
        } catch (const std::exception& e) {
            log::error_fmt("FileService: list_files error for {}: {}", dir_path, e.what());
        }
        return entries;
    };

    // 聚合注册各 API 子模块
    register_api_routes(*router_, session_svc, config_svc, ws_svc, mcp_svc, file_svc);

    container::Vector<container::String> origins;
    if (!settings_.server.cors_origins.empty()) origins = settings_.server.cors_origins;
    else origins.push_back(container::String("*"));
    router_->set_cors_origins(origins);
    log::info_fmt("Server: {} total routes", router_->match_count());
}

void Server::run() {
    running_.store(true);
    auto listen_socket = net::tcp_listen(std::string_view(settings_.server.host.c_str()), settings_.server.port, 64);
    if (!listen_socket.valid()) {
        log::error_fmt("Server: failed to listen on {}:{}", settings_.server.host.c_str(), settings_.server.port);
        return;
    }
    log::info_fmt("Server: listening on {}:{}", settings_.server.host.c_str(), settings_.server.port);
    net::sync_wait(io_context_->loop(), accept_loop(std::move(listen_socket)));
    log::info_fmt("Server: stopped");
}

void Server::stop() {
    if (!running_.exchange(false)) return;
    io_context_->drain();
    log::info_fmt("Server: stopping...");
}

net::Task<void> Server::accept_loop(net::Socket listen_socket) {
    net::set_non_blocking(listen_socket.get());
    log::info_fmt("Server: accept_loop started");
    while (running_.load()) {
        try {
            log::debug_fmt("Server: waiting for connection");
            co_await io_context_->loop().wait_read(listen_socket.get());
            log::debug_fmt("Server: incoming connection");
            while (running_.load()) {
                auto client_fd = net::tcp_accept(listen_socket.get());
                if (!client_fd.valid()) break;
                log::info_fmt("Server: accepted fd={}", client_fd.get());
                net::set_non_blocking(client_fd.get());
                auto stream = std::make_shared<net::TcpStream>(io_context_->loop(), std::move(client_fd));
                net::fire_and_forget(io_context_->loop(), handle_connection(std::move(*stream)));
            }
        } catch (const std::exception& e) {
            if (running_.load()) log::error_fmt("Server: accept error: {}", e.what());
        }
    }
    co_return;
}

net::Task<void> Server::handle_connection(net::TcpStream stream) {
    try {
        auto raw = co_await read_http_request(stream);
        if (raw.empty()) co_return;
        auto req = parse_http(raw);
        if (req.method.empty()) co_return;
        if (req.method == container::String("OPTIONS")) {
            HttpResponse resp; resp.status = 204;
            router_->apply_cors(req, resp);
            co_await send_response(stream, resp);
            co_return;
        }
        std::string origin;
        if (auto it = req.headers.find("origin"); it != req.headers.end()) origin = it->second;
        if (is_ws_upgrade(std::string(req.method.c_str()), std::string(req.path.c_str()),
                          std::map<std::string, std::string>(req.headers.begin(), req.headers.end()))) {
            std::string ws_key;
            if (auto it = req.headers.find("sec-websocket-key"); it != req.headers.end()) ws_key = it->second;
            std::string username;
            if (!authenticate(req, settings_.server, username)) {
                HttpResponse resp = HttpResponse::error(401, "unauthorized");
                router_->apply_cors(req, resp);
                co_await send_response(stream, resp);
                co_return;
            }
            req.username = container::String(username.c_str());
            co_await handle_websocket(std::move(stream), ws_key, origin, container::String(username.c_str()));
            co_return;
        }
        HttpResponse resp;
        auto* handler = router_->match(req.method, req.path, req);
        if (handler) {
            std::string username;
            if (!authenticate(req, settings_.server, username)) {
                resp = HttpResponse::error(401, "unauthorized");
            } else {
                req.username = container::String(username.c_str());
                resp = (*handler)(req);
            }
        } else {
            if (static_files_ && static_files_->valid()) {
                auto file_resp = static_files_->serve(std::string(req.path.c_str()));
                if (file_resp) {
                    HttpResponse hr; hr.status = 200;
                    hr.headers["Content-Type"] = container::String(file_resp->content_type.c_str());
                    hr.headers["Content-Length"] = container::String(std::to_string(file_resp->content_length));
                    hr.body = std::move(file_resp->content);
                    router_->apply_cors(req, hr);
                    co_await send_response(stream, hr);
                    co_return;
                }
            }
            resp = HttpResponse::not_found();
        }
        router_->apply_cors(req, resp);
        co_await send_response(stream, resp);
    } catch (const std::exception& e) { log::warn_fmt("Server: connection error: {}", e.what()); }
}

net::Task<void> Server::send_response(net::TcpStream& stream, const HttpResponse& resp) {
    container::String buf;
    buf.append("HTTP/1.1 "); buf.append(container::String(std::to_string(resp.status))); buf.append(" ");
    static const container::Map<int, container::String> st = {
        {200,"OK"},{201,"Created"},{204,"No Content"},{400,"Bad Request"},
        {401,"Unauthorized"},{403,"Forbidden"},{404,"Not Found"},{500,"Internal Server Error"}
    };
    auto it = st.find(resp.status);
    buf.append(it != st.end() ? it->second : container::String("OK"));
    buf.append("\r\nContent-Length: "); buf.append(container::String(std::to_string(resp.body.size())));
    buf.append("\r\n");
    if (resp.headers.find("Content-Type") == resp.headers.end())
        buf.append("Content-Type: application/json; charset=utf-8\r\n");
    for (const auto& [k, v] : resp.headers) { buf.append(k); buf.append(": "); buf.append(v); buf.append("\r\n"); }
    buf.append("Connection: close\r\n\r\n");
    co_await stream.write_all(std::string_view(buf.data(), buf.size()));
    if (!resp.body.empty()) co_await stream.write_all(resp.body);
}

net::Task<void> Server::handle_websocket(net::TcpStream stream, const std::string& ws_key,
                                          const std::string& origin, const container::String& username) {
    auto ws = std::make_shared<WsHandler>(std::move(stream), ws_key);
    try { co_await ws->handshake(origin); }
    catch (const std::exception& e) { log::error_fmt("Server: WS handshake failed: {}", e.what()); co_return; }
    log::info_fmt("Server: WS connected user={}", username.c_str());
    try {
        auto user_dir = user_dir_for(username);
        auto ws_manager = workspace::WorkspaceManager(user_dir);
        container::String ws_name;
        auto all_ws = ws_manager.list_all();
        if (!all_ws.empty()) {
            ws_name = all_ws[0].name;
        } else {
            // 新用户：自动创建 default workspace 和一个默认会话
            ws_name = container::String("default");
            ws_manager.create(ws_name, {});
            log::info_fmt("Server: created default workspace for new user={}", username.c_str());
        }

        auto db_path = user_dir_for(username) / "history.db";
        workspace::HistoryDB db(db_path);
        auto existing = db.list_sessions(ws_name);
        container::String session_id;
        if (!existing.empty()) {
            session_id = container::String(existing[0].value("session_id", "").c_str());
        }

        std::string cfg = "{\"model\":\"" + std::string(settings_.model.c_str())
            + "\",\"provider\":\"" + std::string(provider_name(settings_.provider).c_str())
            + "\",\"workspace\":\"" + std::string(ws_name.c_str()) + "\"}";
        auto connected = WsMessage::connected(session_id, cfg);
        connected.strings[container::String("workspace")] = ws_name;
        log::info_fmt("Server: WS init user={} workspace={} session={} existing_sessions={}",
                      username.c_str(), ws_name.c_str(), session_id.c_str(), existing.size());
        co_await ws->send_text(connected.to_json());
    } catch (const std::exception& e) { log::error_fmt("Server: WS init send failed: {}", e.what()); }
    co_await ws->read_loop(
        [this, ws, username](std::string_view msg) { on_ws_message(ws, username, msg); },
        [username]() { log::info_fmt("Server: WS disconnected user={}", username.c_str()); });
}

void Server::on_ws_message(std::shared_ptr<WsHandler> ws, const container::String& username, std::string_view message) {
    auto msg = WsMessage::from_json(std::string(message));
    log::debug_fmt("Server: WS msg type={} session={}", msg.type.c_str(), msg.session_id.c_str());
    if (msg.type == "chat") {
        auto pit = msg.strings.find("prompt");
        if (pit == msg.strings.end()) return;
        auto prompt = pit->second;
        // 支持 workspace 参数：优先从消息中取，否则用 settings 里的
        auto workspace = container::String(settings_.workspace_name.c_str());
        auto wit = msg.strings.find("workspace");
        if (wit != msg.strings.end() && !wit->second.empty())
            workspace = wit->second;
        auto entry = get_or_create_agent_session(msg.session_id, username, workspace);
        auto callbacks = std::make_shared<ServerCallbacks>(ws, msg.session_id, workspace);
        net::fire_and_forget(io_context_->loop(),
            handle_ws_chat(ws, callbacks, entry->session->session_id(), container::String(prompt.c_str()), entry));
    } else if (msg.type == "abort") {
        log::info_fmt("Server: abort session={}", msg.session_id.c_str());
    } else if (msg.type == "ping") {
        // 最优方案：走 urgent 队列，flush_writes 在每帧间隙优先发送
        // 确保 pong 不被 write_queue 中的大 token 帧阻塞
        // 相比 queue_send_front：urgent_queue 在每帧 write_all 完成后、取下一帧前检查
        // 即使当前 token 帧的 write_all 正在 yield 等待 socket，urgent 也会等当前帧
        // 发完立刻发送，而不是等到整个 write_queue 清空
        ws->queue_send_urgent(WsMessage::pong().to_json());
    }
}

net::Task<void> Server::handle_ws_chat(std::shared_ptr<WsHandler> ws, std::shared_ptr<ServerCallbacks> callbacks,
                                        container::String session_id, container::String prompt,
                                        std::shared_ptr<SessionEntry> entry) {
    log::info_fmt("Server: chat session={} prompt_len={}", session_id.c_str(), prompt.size());
    auto send_terminal = [&](const llm::ChatResult& result) {
        const auto outcome_json = llm::to_json(result.outcome);
        const auto usage_json = callbacks->response_usage_json();
        const auto latency = callbacks->response_latency();
        const double total_seconds = latency.total_seconds;
        const double ttfb_seconds = latency.has_ttfb ? latency.ttfb_seconds : 0.0;
        log::info_fmt("Server: enqueue terminal session={} status={} reason={} ok={} ws_alive={} queue={} flushing={} usage_len={} outcome_len={}",
                      session_id.c_str(), static_cast<int>(result.status), llm::to_string(result.outcome.reason),
                      result.outcome.ok(), ws->alive(), ws->queue_size(), ws->is_flushing(), usage_json.size(), outcome_json.size());
        if (!result.outcome.ok()) {
            auto message = result.error_message.empty() ? result.outcome.message : result.error_message;
            auto error_json = callbacks->enrich(WsMessage::error_msg(session_id, message, outcome_json)).to_json();
            log::info_fmt("Server: enqueue terminal error session={} workspace={} reason={} msg_len={} frame_len={}",
                          session_id.c_str(), entry->session->workspace_context().workspace_name.c_str(),
                          llm::to_string(result.outcome.reason), message.size(), error_json.size());
            ws->queue_send(std::move(error_json));
        }
        auto done_json = callbacks->enrich(WsMessage::done_with_outcome(session_id, usage_json, outcome_json, total_seconds, ttfb_seconds)).to_json();
        log::info_fmt("Server: enqueue terminal done session={} reason={} frame_len={}",
                      session_id.c_str(), llm::to_string(result.outcome.reason), done_json.size());
        ws->queue_send(std::move(done_json));
    };

    try {
        auto result = co_await entry->agent->run_session_async(io_context_->loop(), *entry->session, container::String(prompt), *callbacks);
        log::info_fmt("Server: chat done session={} status={} outcome={}",
                      session_id.c_str(), static_cast<int>(result.status),
                      llm::to_string(result.outcome.reason));
        if (!result.error_message.empty() || result.is_context_overflow || !result.outcome.ok()) {
            log::warn_fmt("Server: chat result detail session={} is_context_overflow={} reason={} error={}",
                          session_id.c_str(), result.is_context_overflow,
                          llm::to_string(result.outcome.reason), result.error_message.c_str());
        }
        send_terminal(result);
    } catch (const net::OperationCancelled& e) {
        log::warn_fmt("Server: chat cancelled: {}", e.what());
        auto result = llm::ChatResult::cancelled(container::String(e.what()));
        send_terminal(result);
    } catch (const std::exception& e) {
        log::error_fmt("Server: chat error: {}", e.what());
        auto result = llm::ChatResult::internal_error(container::String(e.what()));
        send_terminal(result);
    }
}

std::shared_ptr<SessionEntry> Server::get_or_create_agent_session(
    const container::String& session_id, const container::String& username, const container::String& workspace) {
    auto project_path = project_path_for(username, workspace);
    auto tier_paths = tier_paths_for(username, workspace);
    auto ws_ctx = workspace::WorkspaceContext{
        tier_paths,
        workspace, project_path, username, session_id};
    log::info_fmt("Server: get_or_create_agent_session user={} workspace={} session={} workspace_dir={} project_path={}",
                  username.c_str(), workspace.c_str(), session_id.c_str(),
                  tier_paths.workspace_dir.string().c_str(), project_path.c_str());
    return session_pool_->get_or_create(session_id, username, workspace, settings_, ws_ctx);
}

} // namespace ben_gear::server
