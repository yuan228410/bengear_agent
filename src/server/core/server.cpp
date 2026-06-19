#include "ben_gear/server/core/server.hpp"
#include "ben_gear/server/ws/protocol.hpp"
#include "ben_gear/server/http/parser.hpp"
#include "ben_gear/server/auth/auth.hpp"
#include "ben_gear/server/api/handlers.hpp"
#include "ben_gear/server/api/file_api.hpp"
#include "ben_gear/git/git_service.hpp"
#include "ben_gear/patch/diff_parser.hpp"
#include "ben_gear/patch/patch_service.hpp"
#include "ben_gear/base/log/logger.hpp"
#include "ben_gear/base/net/cancel.hpp"
#include "ben_gear/base/tier_paths.hpp"
#include "ben_gear/llm/run_outcome.hpp"
#include "ben_gear/orchestration/plan_parser.hpp"
#include "ben_gear/orchestration/serializer.hpp"
#include "ben_gear/base/platform/platform.hpp"
#include "ben_gear/workspace/uuid.hpp"
#include "ben_gear/workspace/manager.hpp"
#include "ben_gear/workspace/history_exporter.hpp"

#include <chrono>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace ben_gear::server {

namespace {

container::String json_field(const Json& json, std::string_view key) {
    return json.value(key, "");
}

int json_int_field(const Json& json, std::string_view key, int fallback = 0) {
    return json.value(key, fallback);
}

bool msg_bool_field(const WsMessage& msg, std::string_view key, bool fallback = false) {
    auto it = msg.ints.find(container::String(key));
    if (it != msg.ints.end()) return it->second != 0;
    return fallback;
}

bool json_bool_field(const Json& json, std::string_view key, bool fallback = false) {
    return json.value(key, fallback);
}

Json parse_message_data(const WsMessage& msg, std::string& error) {
    if (msg.json_data.empty()) return Json::object();
    auto json = parse_json(std::string_view(msg.json_data.data(), msg.json_data.size()), error);
    if (!error.empty() || !json.is_object()) {
        if (error.empty()) error = "message data must be a JSON object";
        return Json();
    }
    return json;
}

void queue_ws(std::shared_ptr<WsHandler> ws, WsMessage msg) {
    if (!ws || !ws->alive()) return;
    auto json = msg.to_json();
    auto& loop = ws->loop();
    if (loop.is_loop_thread()) {
        ws->queue_send(std::move(json));
    } else {
        loop.submit_task([ws, json = std::move(json)]() mutable {
            if (ws && ws->alive()) ws->queue_send(std::move(json));
        });
    }
}

void emit_plan_state(std::shared_ptr<WsHandler> ws, const orchestration::PlanDraft& draft) {
    auto payload = orchestration::to_json_string(draft);
    auto msg = WsMessage::plan_state(draft.session_id, std::string(payload.data(), payload.size()));
    if (!draft.workspace.empty()) msg.strings[container::String("workspace")] = draft.workspace;
    queue_ws(std::move(ws), std::move(msg));
}

void emit_todo_state(std::shared_ptr<WsHandler> ws, const orchestration::TodoState& state) {
    auto payload = orchestration::to_json_string(state);
    auto msg = WsMessage::todo_state(state.session_id, std::string(payload.data(), payload.size()));
    if (!state.workspace.empty()) msg.strings[container::String("workspace")] = state.workspace;
    queue_ws(std::move(ws), std::move(msg));
}

void emit_plan_delta(std::shared_ptr<WsHandler> ws, const orchestration::PlanDraft& draft, const Json& delta) {
    auto payload = delta.dump().to_std_string();
    auto msg = WsMessage::plan_delta(draft.session_id, payload);
    if (!draft.workspace.empty()) msg.strings[container::String("workspace")] = draft.workspace;
    queue_ws(std::move(ws), std::move(msg));
}

void persist_plan_state(SessionEntry& entry) {
    auto payload = orchestration::to_json_string(entry.plan_manager.draft());
    const auto& draft = entry.plan_manager.draft();
    entry.agent->history_db().save_session_state(draft.workspace, draft.session_id, container::String("plan"), payload);
}

void persist_todo_state(SessionEntry& entry) {
    auto payload = orchestration::to_json_string(entry.todo_manager.state());
    const auto& state = entry.todo_manager.state();
    entry.agent->history_db().save_session_state(state.workspace, state.session_id, container::String("todo"), payload);
}

container::String build_execution_prompt(const orchestration::PlanDraft& plan) {
    std::string prompt =
        "Execute the approved final plan exactly. Use final_items and preserve every user-selected decision.\n"
        "Keep the visible TODO list accurate in real time: before starting each final plan item, call update_todo with that item status=running and progress=0; when the item completes, call update_todo with status=succeeded and progress=100; if it fails or blocks, call update_todo with status=failed or blocked and include result_summary. Do not wait until the whole plan is done to update TODOs.\n";
    prompt += "Final plan JSON:\n";
    auto json = orchestration::to_json_string(plan);
    prompt.append(json.data(), json.size());
    return container::String(prompt.c_str(), prompt.size());
}

orchestration::PlanFinalDraft build_local_final_draft(const orchestration::PlanDraft& draft) {
    orchestration::PlanFinalDraft final_draft;
    final_draft.summary = draft.title.empty() ? container::String("Approved plan ready for execution") : draft.title;
    final_draft.items = draft.items;
    final_draft.global_risks = draft.global_risks;
    final_draft.validation = draft.validation;
    final_draft.consistency_notes.push_back(container::String("Fast local synthesis used; user-selected decisions are preserved."));

    for (auto& item : final_draft.items) {
        if (item.decisions.empty()) continue;
        std::string desc(item.description.data(), item.description.size());
        bool wrote_header = false;
        for (const auto& decision : item.decisions) {
            container::String selected;
            if (!decision.custom_note.empty()) {
                selected = decision.custom_note;
            } else {
                for (const auto& choice : decision.choices) {
                    if (choice.id == decision.selected_choice_id) {
                        selected = choice.title.empty() ? choice.description : choice.title;
                        break;
                    }
                }
            }
            if (selected.empty()) continue;
            if (!wrote_header) {
                if (!desc.empty()) desc += " ";
                desc += "Selected decisions:";
                wrote_header = true;
            }
            desc += " ";
            desc.append(decision.title.data(), decision.title.size());
            desc += " = ";
            desc.append(selected.data(), selected.size());
            desc += ";";
        }
        item.description = container::String(desc.c_str(), desc.size());
    }
    return final_draft;
}

bool is_continue_prompt(std::string_view prompt) {
    while (!prompt.empty() && (prompt.front() == ' ' || prompt.front() == '\n' || prompt.front() == '\t' || prompt.front() == '\r')) prompt.remove_prefix(1);
    while (!prompt.empty() && (prompt.back() == ' ' || prompt.back() == '\n' || prompt.back() == '\t' || prompt.back() == '\r' || prompt.back() == '.' || prompt.back() == '!' || prompt.back() == '?')) prompt.remove_suffix(1);
    return prompt == "继续" || prompt == "继续执行" || prompt == "继续进行" || prompt == "continue" || prompt == "resume";
}

container::String maybe_append_continue_context(container::String prompt, const orchestration::TodoManager& todo_manager) {
    if (todo_manager.empty() || !is_continue_prompt(std::string_view(prompt.data(), prompt.size()))) return prompt;
    prompt.append("\n\nResume the previous interrupted task using the current TODO state. Continue pending or blocked work, do not repeat succeeded work, and use update_todo to refine or update TODO items when useful.");
    return prompt;
}

} // namespace

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
        return db.load_session_chat_messages(ws, sid, limit);
    };
    session_svc.export_history = [this](const container::String& sid, const container::String& ws_name, bool include_tool_calls, bool include_thinking, bool include_tool_results, int limit, const container::String& username) {
        container::String ws = ws_name.empty() ? container::String(settings_.workspace_name.c_str()) : ws_name;
        auto db_path = user_dir_for(username) / "history.db";
        workspace::HistoryDB db(db_path);
        workspace::ExportOptions opts;
        opts.include_tool_calls = include_tool_calls;
        opts.include_thinking = include_thinking;
        opts.include_tool_results = include_tool_results;
        opts.limit = limit;
        return workspace::HistoryExporter::export_session_md(db, ws, sid, opts);
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

    GitApiService git_svc;
    auto make_git_service = [this](const container::String& workspace,
                                   const container::String& username) {
        auto ws = workspace.empty() ? container::String(settings_.workspace_name.c_str()) : workspace;
        auto ws_ctx = workspace::WorkspaceContext{
            tier_paths_for(username, ws),
            ws,
            project_path_for(username, ws),
            username,
            container::String()};
        return git::GitService(ws_ctx);
    };
    git_svc.status = [make_git_service](const container::String& workspace,
                                        const container::String& username) {
        return git::to_json(make_git_service(workspace, username).status());
    };
    git_svc.diff = [make_git_service](const container::String& workspace,
                                      const container::String& username,
                                      std::string_view path,
                                      bool staged,
                                      bool stat,
                                      bool preview) {
        auto result = make_git_service(workspace, username).diff(std::string(path), staged, stat);
        if (!result.value("success", false)) return result;
        result["path"] = std::string(path);
        result["empty"] = result.value("diff", "").empty();
        if (preview && !stat) {
            auto parsed = result.value("empty", false) ? patch::empty_patch_preview() : patch::parse_unified_diff(result.value("diff", ""));
            parsed.can_apply = false;
            result["preview"] = patch::to_json(parsed);
        }
        return result;
    };
    git_svc.log = [make_git_service](const container::String& workspace,
                                     const container::String& username,
                                     std::string_view path,
                                     int limit) {
        auto result = make_git_service(workspace, username).log(limit, std::string(path));
        if (result.value("success", false)) result["path"] = std::string(path);
        return result;
    };
    git_svc.branches = [make_git_service](const container::String& workspace,
                                          const container::String& username) {
        return make_git_service(workspace, username).branch("list");
    };

    PatchApiService patch_svc;
    auto make_patch_service = [this](const container::String& workspace,
                                     const container::String& session_id,
                                     const container::String& username) {
        auto ws = workspace.empty() ? container::String(settings_.workspace_name.c_str()) : workspace;
        auto ws_ctx = workspace::WorkspaceContext{
            tier_paths_for(username, ws),
            ws,
            project_path_for(username, ws),
            username,
            session_id};
        return patch::PatchService(ws_ctx);
    };
    patch_svc.preview_patch = [make_patch_service](const container::String& workspace,
                                                   const container::String& session_id,
                                                   const container::String& username,
                                                   std::string_view unified_diff) {
        auto service = make_patch_service(workspace, session_id, username);
        return patch::to_json(service.preview(unified_diff));
    };
    patch_svc.apply_patch = [make_patch_service](const container::String& workspace,
                                                 const container::String& session_id,
                                                 const container::String& username,
                                                 std::string_view unified_diff,
                                                 std::string_view description) {
        auto service = make_patch_service(workspace, session_id, username);
        return service.apply(unified_diff, description);
    };
    patch_svc.list_changes = [make_patch_service](const container::String& workspace,
                                                  const container::String& session_id,
                                                  const container::String& username) {
        auto service = make_patch_service(workspace, session_id, username);
        return service.list_changes();
    };
    patch_svc.read_change = [make_patch_service](const container::String& workspace,
                                                 const container::String& session_id,
                                                 const container::String& username,
                                                 std::string_view change_id) {
        auto service = make_patch_service(workspace, session_id, username);
        auto result = service.read_change(change_id);
        if (result.value("success", false)) {
            Json change = result["change"];
            Json files = change["files"];
            if (files.is_array()) {
                Json safe_files = Json::array();
                for (size_t i = 0; i < files.size(); ++i) {
                    Json file = files[i];
                    file.erase("before_content");
                    safe_files.push_back(std::move(file));
                }
                change["files"] = std::move(safe_files);
                result["change"] = std::move(change);
            }
        }
        return result;
    };
    patch_svc.revert_change = [make_patch_service](const container::String& workspace,
                                                   const container::String& session_id,
                                                   const container::String& username,
                                                   std::string_view change_id,
                                                   bool force) {
        auto service = make_patch_service(workspace, session_id, username);
        return service.revert(change_id, force);
    };

    // 聚合注册各 API 子模块
    register_api_routes(*router_, session_svc, config_svc, ws_svc, mcp_svc, file_svc, git_svc, patch_svc);

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
                net::fire_and_forget(io_context_->loop(), handle_connection(net::TcpStream(io_context_->loop(), std::move(client_fd))));
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
    } catch (const std::exception& e) {
        log::warn_fmt("Server: connection error: {}", e.what());
        stream.close();
    }
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
    stream.close();
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

        Json cfg;
        cfg["model"] = settings_.model;
        cfg["provider"] = provider_name(settings_.provider);
        cfg["workspace"] = ws_name;
        auto connected = WsMessage::connected(session_id, cfg.dump());
        connected.strings[container::String("workspace")] = ws_name;
        log::info_fmt("Server: WS init user={} workspace={} session={} existing_sessions={}",
                      username.c_str(), ws_name.c_str(), session_id.c_str(), existing.size());
        co_await ws->send_text(connected.to_json());
        if (!session_id.empty()) {
            auto entry = get_or_create_agent_session(session_id, username, ws_name);
            auto plan_payload = orchestration::to_json_string(entry->plan_manager.draft());
            auto plan_msg = WsMessage::plan_state(session_id, std::string(plan_payload.data(), plan_payload.size()));
            plan_msg.strings[container::String("workspace")] = ws_name;
            co_await ws->send_text(plan_msg.to_json());
            auto todo_payload = orchestration::to_json_string(entry->todo_manager.state());
            auto todo_msg = WsMessage::todo_state(session_id, std::string(todo_payload.data(), todo_payload.size()));
            todo_msg.strings[container::String("workspace")] = ws_name;
            co_await ws->send_text(todo_msg.to_json());
        }
    } catch (const std::exception& e) { log::error_fmt("Server: WS init send failed: {}", e.what()); }
    co_await ws->read_loop(
        [this, ws, username](std::string_view msg) { on_ws_message(ws, username, msg); },
        [username]() { log::info_fmt("Server: WS disconnected user={}", username.c_str()); });
}

void Server::on_ws_message(std::shared_ptr<WsHandler> ws, const container::String& username, std::string_view message) {
    auto msg = WsMessage::from_json(std::string(message));
    log::debug_fmt("Server: WS msg type={} session={}", msg.type.c_str(), msg.session_id.c_str());
    auto workspace = container::String(settings_.workspace_name.c_str());
    auto wit = msg.strings.find("workspace");
    if (wit != msg.strings.end() && !wit->second.empty()) workspace = wit->second;

    if (msg.type == "chat") {
        auto pit = msg.strings.find("prompt");
        if (pit == msg.strings.end()) return;
        auto prompt = pit->second;
        auto entry = get_or_create_agent_session(msg.session_id, username, workspace);
        prompt = maybe_append_continue_context(std::move(prompt), entry->todo_manager);
        auto callbacks = std::make_shared<ServerCallbacks>(
            ws, msg.session_id, workspace,
            msg_bool_field(msg, "include_thinking"),
            msg_bool_field(msg, "include_tool_calls"),
            &entry->todo_manager, &entry->agent->history_db());
        callbacks->set_state_mutex(&entry->state_mutex);
        auto chat_context = entry->agent->resources()->io_context();
        net::fire_and_forget(chat_context->loop(),
            handle_ws_chat(ws, callbacks, entry->session->session_id(), container::String(prompt.c_str()), entry));
    } else if (msg.type == "switch") {
        auto entry = get_or_create_agent_session(msg.session_id, username, workspace);
        emit_plan_state(ws, entry->plan_manager.draft());
        emit_todo_state(ws, entry->todo_manager.state());
    } else if (msg.type == "plan_start" || msg.type == "plan_chat" || msg.type == "plan_update_items" ||
               msg.type == "plan_select_option" || msg.type == "plan_apply_choice" || msg.type == "plan_apply_decision" ||
               msg.type == "plan_finalize" || msg.type == "plan_confirm" || msg.type == "plan_cancel" ||
               msg.type == "todo_update") {
        std::string error;
        auto data = parse_message_data(msg, error);
        if (!error.empty()) {
            queue_ws(ws, WsMessage::error_msg(msg.session_id, container::String(error.c_str())));
            return;
        }
        auto entry = get_or_create_agent_session(msg.session_id, username, workspace);
        auto include_thinking = json_bool_field(data, "include_thinking");
        auto include_tool_calls = json_bool_field(data, "include_tool_calls");
        auto callbacks = std::make_shared<ServerCallbacks>(
            ws, msg.session_id, workspace,
            include_thinking, include_tool_calls,
            &entry->todo_manager, &entry->agent->history_db());
        callbacks->set_state_mutex(&entry->state_mutex);
        auto chat_context = entry->agent->resources()->io_context();
        if (msg.type == "plan_start") {
            auto prompt = json_field(data, "prompt");
            auto note = json_field(data, "note");
            net::fire_and_forget(chat_context->loop(), handle_ws_plan_start(ws, entry->session->session_id(), prompt, note, entry));
        } else if (msg.type == "plan_chat") {
            PlanChatRequest request;
            request.mode = json_field(data, "mode");
            if (request.mode.empty()) request.mode = "revise";
            request.revision = json_int_field(data, "revision");
            request.note = json_field(data, "note");
            if (request.note.empty()) request.note = json_field(data, "prompt");
            request.custom_idea = json_field(data, "custom_idea");
            request.item_id = json_field(data, "item_id");
            request.decision_id = json_field(data, "decision_id");
            net::fire_and_forget(chat_context->loop(), handle_ws_plan_chat(ws, entry->session->session_id(), std::move(request), entry));
        } else if (msg.type == "plan_update_items") {
            container::Vector<orchestration::PlanItem> items;
            auto raw_items = data["items"];
            if (raw_items.is_array()) {
                for (size_t i = 0; i < raw_items.size(); ++i) items.push_back(orchestration::plan_item_from_json(raw_items[i]));
            }
            net::fire_and_forget(chat_context->loop(), handle_ws_plan_update_items(ws, entry->session->session_id(), std::move(items), entry));
        } else if (msg.type == "plan_select_option") {
            net::fire_and_forget(chat_context->loop(), handle_ws_plan_select_option(ws, entry->session->session_id(), json_field(data, "option_id"), json_int_field(data, "revision"), entry));
        } else if (msg.type == "plan_apply_choice" || msg.type == "plan_apply_decision") {
            orchestration::PlanDecisionPatch patch;
            patch.revision = json_int_field(data, "revision");
            patch.item_id = json_field(data, "item_id");
            patch.decision_id = json_field(data, "decision_id");
            patch.choice_id = json_field(data, "choice_id");
            patch.custom_note = json_field(data, "custom_note");
            net::fire_and_forget(chat_context->loop(), handle_ws_plan_apply_decision(ws, entry->session->session_id(), std::move(patch), entry));
        } else if (msg.type == "plan_finalize") {
            net::fire_and_forget(chat_context->loop(), handle_ws_plan_finalize(ws, entry->session->session_id(), json_int_field(data, "revision"), entry));
        } else if (msg.type == "plan_confirm") {
            container::Vector<orchestration::PlanItem> items;
            auto raw_items = data["items"];
            const bool has_items = raw_items.is_array();
            if (has_items) {
                for (size_t i = 0; i < raw_items.size(); ++i) items.push_back(orchestration::plan_item_from_json(raw_items[i]));
            }
            net::fire_and_forget(chat_context->loop(), handle_ws_plan_confirm(ws, callbacks, entry->session->session_id(), json_int_field(data, "revision"), has_items, std::move(items), entry));
        } else if (msg.type == "plan_cancel") {
            net::fire_and_forget(chat_context->loop(), handle_ws_plan_cancel(ws, entry->session->session_id(), entry));
        } else if (msg.type == "todo_update") {
            net::fire_and_forget(chat_context->loop(), handle_ws_todo_update(ws, orchestration::todo_item_from_json(data["item"]), entry));
        }
    } else if (msg.type == "abort") {
        log::info_fmt("Server: abort session={}", msg.session_id.c_str());
        if (!session_pool_->cancel(msg.session_id, username, workspace)) {
            log::debug_fmt("Server: abort ignored session={} (not running)", msg.session_id.c_str());
        }
    } else if (msg.type == "ping") {
        // 最优方案：走 urgent 队列，flush_writes 在每帧间隙优先发送
        // 确保 pong 不被 write_queue 中的大 token 帧阻塞
        // 相比 queue_send_front：urgent_queue 在每帧 write_all 完成后、取下一帧前检查
        // 即使当前 token 帧的 write_all 正在 yield 等待 socket，urgent 也会等当前帧
        // 发完立刻发送，而不是等到整个 write_queue 清空
        ws->queue_send_urgent(WsMessage::pong().to_json());
    }
}

net::Task<void> Server::handle_ws_plan_start(std::shared_ptr<WsHandler> ws,
                                             container::String session_id,
                                             container::String prompt,
                                             container::String note,
                                             std::shared_ptr<SessionEntry> entry) {
    if (prompt.empty()) {
        queue_ws(ws, WsMessage::error_msg(session_id, container::String("plan prompt is empty")));
        co_return;
    }
    orchestration::PlanCommand command;
    command.session_id = session_id;
    command.workspace = entry->session->workspace_context().workspace_name;
    command.prompt = prompt;
    command.note = note;
    {
        std::lock_guard state_lock(entry->state_mutex);
        entry->plan_manager.start(command);
        entry->session->persist_message(container::String("user"), prompt, entry->agent->history_db());
        entry->session->persist_message(container::String("plan_anchor"), container::String(), entry->agent->history_db());
        persist_plan_state(*entry);
        emit_plan_state(ws, entry->plan_manager.draft());
    }

    container::String previous_error;
    container::String previous_output;
    orchestration::PlanParseResult parsed;
    auto& agent_loop = entry->agent->resources()->io_context()->loop();
    for (int attempt = 0; attempt < 3; ++attempt) {
        auto user_prompt = orchestration::build_plan_options_prompt(prompt, note, previous_error, previous_output);
        llm::ChatRequest request;
        request.system_prompt = "Return structured JSON only for the web plan option review state.";
        request.user_prompt = user_prompt;
        auto result = co_await entry->agent->resources()->provider().chat_async(agent_loop, request);
        previous_output = result.text;
        if (!result.ok()) {
            previous_error = result.error_message.empty() ? container::String("LLM request failed") : result.error_message;
            continue;
        }
        parsed = orchestration::parse_plan_options_text(std::string_view(result.text.data(), result.text.size()), session_id, command.workspace, prompt);
        if (parsed.ok) break;
        previous_error = parsed.error;
    }

    {
        std::lock_guard state_lock(entry->state_mutex);
        if (!parsed.ok) {
            entry->plan_manager.mark_failed(previous_error.empty() ? container::String("failed to parse plan after retries") : previous_error);
            persist_plan_state(*entry);
            emit_plan_state(ws, entry->plan_manager.draft());
            co_return;
        }

        parsed.draft.plan_id = entry->plan_manager.draft().plan_id;
        entry->plan_manager.restore(std::move(parsed.draft));
        persist_plan_state(*entry);
        emit_plan_state(ws, entry->plan_manager.draft());
    }
}

net::Task<void> Server::handle_ws_plan_chat(std::shared_ptr<WsHandler> ws,
                                            container::String session_id,
                                            PlanChatRequest request,
                                            std::shared_ptr<SessionEntry> entry) {
    enum class RevisionKind { options, detail, final };

    auto feedback = request.custom_idea.empty() ? request.note : request.custom_idea;
    if (feedback.empty()) {
        queue_ws(ws, WsMessage::error_msg(session_id, container::String("plan revision feedback is empty")));
        co_return;
    }

    uint64_t request_id = 0;
    RevisionKind kind = RevisionKind::options;
    orchestration::PlanDraft snapshot;
    try {
        std::lock_guard state_lock(entry->state_mutex);
        const auto& current = entry->plan_manager.draft();
        if (request.mode == "reject_options") {
            if (current.stage != orchestration::PlanStage::option_review) throw std::logic_error("plan options can only be revised during option review");
            kind = RevisionKind::options;
        } else if (request.mode == "reject_decision") {
            if (current.stage != orchestration::PlanStage::decision_review && current.stage != orchestration::PlanStage::final_review) {
                throw std::logic_error("plan decisions can only be revised during decision review");
            }
            if (request.item_id.empty() || request.decision_id.empty()) throw std::logic_error("plan decision revision target is empty");
            kind = RevisionKind::detail;
        } else if (request.mode == "revise_final") {
            if (current.stage != orchestration::PlanStage::final_review) throw std::logic_error("final plan can only be revised during final review");
            kind = RevisionKind::final;
        } else {
            if (current.stage == orchestration::PlanStage::option_review) kind = RevisionKind::options;
            else if (current.stage == orchestration::PlanStage::decision_review) kind = RevisionKind::detail;
            else if (current.stage == orchestration::PlanStage::final_review) kind = RevisionKind::final;
            else throw std::logic_error("plan cannot be revised in the current stage");
        }
        request_id = entry->plan_manager.begin_chat_revision(request.revision);
        snapshot = entry->plan_manager.draft();
        persist_plan_state(*entry);
        emit_plan_state(ws, snapshot);
    } catch (const std::exception& e) {
        queue_ws(ws, WsMessage::error_msg(session_id, container::String(e.what())));
        std::lock_guard state_lock(entry->state_mutex);
        emit_plan_state(ws, entry->plan_manager.draft());
        co_return;
    }

    container::String previous_error;
    container::String previous_output;
    orchestration::PlanParseResult parsed;
    auto& agent_loop = entry->agent->resources()->io_context()->loop();
    for (int attempt = 0; attempt < 3; ++attempt) {
        container::String user_prompt;
        if (kind == RevisionKind::options) {
            user_prompt = orchestration::build_plan_options_revision_prompt(snapshot, feedback, previous_error, previous_output);
        } else if (kind == RevisionKind::detail) {
            user_prompt = orchestration::build_plan_decision_revision_prompt(snapshot, request.item_id, request.decision_id, feedback, previous_error, previous_output);
        } else {
            user_prompt = orchestration::build_plan_final_revision_prompt(snapshot, feedback, previous_error, previous_output);
        }
        llm::ChatRequest llm_request;
        llm_request.system_prompt = "Revise the structured plan and return JSON only.";
        llm_request.user_prompt = user_prompt;
        auto result = co_await entry->agent->resources()->provider().chat_async(agent_loop, llm_request);
        previous_output = result.text;
        if (!result.ok()) {
            previous_error = result.error_message.empty() ? container::String("LLM request failed") : result.error_message;
            continue;
        }
        if (kind == RevisionKind::options) {
            parsed = orchestration::parse_plan_options_text(std::string_view(result.text.data(), result.text.size()), session_id, entry->session->workspace_context().workspace_name, snapshot.objective);
        } else if (kind == RevisionKind::detail) {
            parsed = orchestration::parse_plan_detail_text(std::string_view(result.text.data(), result.text.size()), session_id, entry->session->workspace_context().workspace_name, snapshot.objective, snapshot.selected_option_id);
        } else {
            parsed = orchestration::parse_plan_final_text(std::string_view(result.text.data(), result.text.size()), snapshot);
        }
        if (parsed.ok) break;
        previous_error = parsed.error;
    }

    try {
        std::lock_guard state_lock(entry->state_mutex);
        if (!parsed.ok) {
            entry->plan_manager.mark_review_error(previous_error.empty() ? container::String("failed to parse revised plan after retries") : previous_error);
            persist_plan_state(*entry);
            emit_plan_state(ws, entry->plan_manager.draft());
            co_return;
        }

        if (kind == RevisionKind::options) {
            entry->plan_manager.apply_revised_options(request_id,
                                                       std::move(parsed.draft.title),
                                                       std::move(parsed.draft.objective),
                                                       std::move(parsed.draft.options),
                                                       std::move(parsed.draft.selected_option_id));
        } else if (kind == RevisionKind::detail) {
            entry->plan_manager.apply_revised_detail(request_id,
                                                      std::move(parsed.draft.title),
                                                      std::move(parsed.draft.objective),
                                                      std::move(parsed.draft.items),
                                                      std::move(parsed.draft.global_risks),
                                                      std::move(parsed.draft.validation));
        } else {
            orchestration::PlanFinalDraft final_draft;
            final_draft.summary = std::move(parsed.draft.final_summary);
            final_draft.items = std::move(parsed.draft.final_items);
            final_draft.global_risks = std::move(parsed.draft.global_risks);
            final_draft.validation = std::move(parsed.draft.validation);
            final_draft.consistency_notes = std::move(parsed.draft.consistency_notes);
            entry->plan_manager.apply_revised_final(request_id, std::move(final_draft));
        }
        persist_plan_state(*entry);
        emit_plan_state(ws, entry->plan_manager.draft());
    } catch (const std::exception& e) {
        queue_ws(ws, WsMessage::error_msg(session_id, container::String(e.what())));
        std::lock_guard state_lock(entry->state_mutex);
        emit_plan_state(ws, entry->plan_manager.draft());
    }
}

net::Task<void> Server::handle_ws_plan_update_items(std::shared_ptr<WsHandler> ws,
                                                    container::String session_id,
                                                    container::Vector<orchestration::PlanItem> items,
                                                    std::shared_ptr<SessionEntry> entry) {
    try {
        std::lock_guard state_lock(entry->state_mutex);
        entry->plan_manager.apply_user_items(std::move(items));
        persist_plan_state(*entry);
        emit_plan_state(ws, entry->plan_manager.draft());
    } catch (const std::exception& e) {
        queue_ws(ws, WsMessage::error_msg(session_id, container::String(e.what())));
    }
    co_return;
}

net::Task<void> Server::handle_ws_plan_select_option(std::shared_ptr<WsHandler> ws,
                                                     container::String session_id,
                                                     container::String option_id,
                                                     int revision,
                                                     std::shared_ptr<SessionEntry> entry) {
    uint64_t request_id = 0;
    orchestration::PlanDraft snapshot;
    container::String selected_option_id = option_id;
    try {
        {
            std::lock_guard state_lock(entry->state_mutex);
            request_id = entry->plan_manager.begin_detailing(option_id, revision);
            snapshot = entry->plan_manager.draft();
            persist_plan_state(*entry);
            emit_plan_state(ws, snapshot);
        }

        container::String previous_error;
        container::String previous_output;
        orchestration::PlanParseResult parsed;
        auto& agent_loop = entry->agent->resources()->io_context()->loop();
        for (int attempt = 0; attempt < 3; ++attempt) {
            auto user_prompt = orchestration::build_plan_detail_prompt(snapshot, selected_option_id, previous_error, previous_output);
            llm::ChatRequest request;
            request.system_prompt = "Return structured JSON only for the selected plan option detail.";
            request.user_prompt = user_prompt;
            auto result = co_await entry->agent->resources()->provider().chat_async(agent_loop, request);
            previous_output = result.text;
            if (!result.ok()) {
                previous_error = result.error_message.empty() ? container::String("LLM request failed") : result.error_message;
                continue;
            }
            parsed = orchestration::parse_plan_detail_text(std::string_view(result.text.data(), result.text.size()), session_id, entry->session->workspace_context().workspace_name, snapshot.objective, selected_option_id);
            if (parsed.ok) break;
            previous_error = parsed.error;
        }

        std::lock_guard state_lock(entry->state_mutex);
        if (!parsed.ok) {
            entry->plan_manager.mark_failed(previous_error.empty() ? container::String("failed to parse detailed plan after retries") : previous_error);
            persist_plan_state(*entry);
            emit_plan_state(ws, entry->plan_manager.draft());
            co_return;
        }
        entry->plan_manager.apply_model_detail(selected_option_id, request_id,
                                               std::move(parsed.draft.title),
                                               std::move(parsed.draft.objective),
                                               std::move(parsed.draft.items),
                                               std::move(parsed.draft.global_risks),
                                               std::move(parsed.draft.validation));
        persist_plan_state(*entry);
        emit_plan_state(ws, entry->plan_manager.draft());
    } catch (const std::exception& e) {
        queue_ws(ws, WsMessage::error_msg(session_id, container::String(e.what())));
        std::lock_guard state_lock(entry->state_mutex);
        emit_plan_state(ws, entry->plan_manager.draft());
    }
    co_return;
}

net::Task<void> Server::handle_ws_plan_apply_decision(std::shared_ptr<WsHandler> ws,
                                                       container::String session_id,
                                                       orchestration::PlanDecisionPatch patch,
                                                       std::shared_ptr<SessionEntry> entry) {
    bool should_finalize = false;
    int finalize_revision = 0;
    try {
        {
            std::lock_guard state_lock(entry->state_mutex);
            should_finalize = entry->plan_manager.apply_decision(patch);
            const auto& draft = entry->plan_manager.draft();
            finalize_revision = draft.revision;
            persist_plan_state(*entry);
            Json delta{{"event", "plan.apply_decision"},
                       {"session_id", draft.session_id},
                       {"workspace", draft.workspace},
                       {"revision", draft.revision},
                       {"item_id", patch.item_id},
                       {"decision_id", patch.decision_id},
                       {"selected_choice_id", patch.choice_id},
                       {"custom_note", patch.custom_note},
                       {"all_decisions_resolved", should_finalize}};
            emit_plan_delta(ws, draft, delta);
        }
        if (should_finalize) {
            co_await handle_ws_plan_finalize(ws, session_id, finalize_revision, entry);
        }
    } catch (const std::exception& e) {
        queue_ws(ws, WsMessage::error_msg(session_id, container::String(e.what())));
        std::lock_guard state_lock(entry->state_mutex);
        emit_plan_state(ws, entry->plan_manager.draft());
    }
    co_return;
}

net::Task<void> Server::handle_ws_plan_finalize(std::shared_ptr<WsHandler> ws,
                                                 container::String session_id,
                                                 int revision,
                                                 std::shared_ptr<SessionEntry> entry) {
    uint64_t request_id = 0;
    orchestration::PlanDraft snapshot;
    try {
        {
            std::lock_guard state_lock(entry->state_mutex);
            if (entry->plan_manager.draft().stage == orchestration::PlanStage::final_review &&
                entry->plan_manager.draft().finalized_input_revision == revision) {
                emit_plan_state(ws, entry->plan_manager.draft());
                co_return;
            }
            request_id = entry->plan_manager.begin_finalizing(revision);
            snapshot = entry->plan_manager.draft();
            persist_plan_state(*entry);
            emit_plan_state(ws, snapshot);
        }

        // 常规最终整理走本地快速合成，避免用户完成选择后再等待一次模型。
        // 需要模型参与的深度修订仍由 revise_final 路径处理。
        auto final_draft = build_local_final_draft(snapshot);
        std::lock_guard state_lock(entry->state_mutex);
        entry->plan_manager.apply_model_final(request_id, std::move(final_draft));
        persist_plan_state(*entry);
        emit_plan_state(ws, entry->plan_manager.draft());
    } catch (const std::exception& e) {
        queue_ws(ws, WsMessage::error_msg(session_id, container::String(e.what())));
        std::lock_guard state_lock(entry->state_mutex);
        emit_plan_state(ws, entry->plan_manager.draft());
    }
    co_return;
}

net::Task<void> Server::handle_ws_plan_confirm(std::shared_ptr<WsHandler> ws,
                                               std::shared_ptr<ServerCallbacks> callbacks,
                                               container::String session_id,
                                               int revision,
                                               bool has_items,
                                               container::Vector<orchestration::PlanItem> items,
                                               std::shared_ptr<SessionEntry> entry) {
    try {
        (void)has_items;
        (void)items;
        container::String execution_prompt;
        {
            std::lock_guard state_lock(entry->state_mutex);
            entry->plan_manager.confirm(revision);
            auto confirmed = entry->plan_manager.draft();

            entry->todo_manager.initialize_from_plan(confirmed);
            persist_todo_state(*entry);
            emit_todo_state(ws, entry->todo_manager.state());

            entry->plan_manager.mark_executing();
            persist_plan_state(*entry);
            emit_plan_state(ws, entry->plan_manager.draft());
            execution_prompt = build_execution_prompt(entry->plan_manager.draft());
        }

        agent::Agent::RunOptions options;
        options.persist_user_message = false;
        co_await handle_ws_chat(ws, callbacks, session_id, std::move(execution_prompt), entry, std::move(options));
    } catch (const std::exception& e) {
        queue_ws(ws, WsMessage::error_msg(session_id, container::String(e.what())));
        std::lock_guard state_lock(entry->state_mutex);
        emit_plan_state(ws, entry->plan_manager.draft());
    }
}

net::Task<void> Server::handle_ws_plan_cancel(std::shared_ptr<WsHandler> ws,
                                              container::String,
                                              std::shared_ptr<SessionEntry> entry) {
    std::lock_guard state_lock(entry->state_mutex);
    entry->plan_manager.cancel();
    persist_plan_state(*entry);
    emit_plan_state(ws, entry->plan_manager.draft());
    co_return;
}

net::Task<void> Server::handle_ws_todo_update(std::shared_ptr<WsHandler> ws,
                                              orchestration::TodoItem item,
                                              std::shared_ptr<SessionEntry> entry) {
    orchestration::TodoDelta delta;
    container::String state_session_id;
    container::String state_workspace;
    {
        std::lock_guard state_lock(entry->state_mutex);
        delta = entry->todo_manager.upsert(std::move(item), container::String("manual"));
        persist_todo_state(*entry);
        state_session_id = entry->todo_manager.state().session_id;
        state_workspace = entry->todo_manager.state().workspace;
    }
    auto payload = orchestration::to_json_string(delta);
    auto msg = WsMessage::todo_delta(state_session_id, std::string(payload.data(), payload.size()));
    if (!state_workspace.empty()) msg.strings[container::String("workspace")] = state_workspace;
    queue_ws(ws, std::move(msg));
    co_return;
}

net::Task<void> Server::handle_ws_chat(std::shared_ptr<WsHandler> ws, std::shared_ptr<ServerCallbacks> callbacks,
                                        container::String session_id, container::String prompt,
                                        std::shared_ptr<SessionEntry> entry,
                                        agent::Agent::RunOptions options) {
    log::info_fmt("Server: chat session={} prompt_len={}", session_id.c_str(), prompt.size());

    struct ActiveRunGuard {
        std::shared_ptr<SessionEntry> entry;
        net::CancellationToken cancel;
        bool acquired = false;

        explicit ActiveRunGuard(std::shared_ptr<SessionEntry> e)
            : entry(std::move(e)), cancel() {
            std::lock_guard lock(entry->run_mutex);
            if (entry->active_run) return;
            entry->active_cancel = cancel;
            entry->active_run = true;
            acquired = true;
        }

        ~ActiveRunGuard() noexcept {
            if (!acquired) return;
            std::lock_guard lock(entry->run_mutex);
            entry->active_run = false;
            entry->active_cancel = net::CancellationToken();
        }
    } run_guard(entry);

    if (!run_guard.acquired) {
        log::warn_fmt("Server: reject concurrent chat session={}", session_id.c_str());
        queue_ws(ws, WsMessage::error_msg(session_id, container::String("session is already running")));
        co_return;
    }

    auto finalize_todos = [&](const llm::ChatResult& result) {
        if (entry->todo_manager.empty()) return;
        orchestration::TodoStatus status = orchestration::TodoStatus::failed;
        container::String summary = result.outcome.message.empty() ? container::String("execution failed") : result.outcome.message;
        if (result.outcome.ok()) {
            status = orchestration::TodoStatus::succeeded;
            summary = "execution completed";
        } else if (result.outcome.status == llm::RunStatus::cancelled) {
            status = orchestration::TodoStatus::cancelled;
            summary = "execution cancelled";
        } else if (result.outcome.status == llm::RunStatus::interrupted) {
            status = orchestration::TodoStatus::blocked;
            if (summary.empty()) summary = "execution interrupted";
        }
        if (result.outcome.ok()) {
            entry->todo_manager.mark_all_running_as(status, summary);
        } else {
            entry->todo_manager.mark_running_as(status, summary);
        }
        persist_todo_state(*entry);
        emit_todo_state(ws, entry->todo_manager.state());
    };
    auto send_terminal = [&](const llm::ChatResult& result) {
        finalize_todos(result);
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
            auto ws_for_error = ws;
            ws->loop().submit_task([ws_for_error, error_json = std::move(error_json)]() mutable {
                if (ws_for_error && ws_for_error->alive()) ws_for_error->queue_send(std::move(error_json));
            });
        }
        auto done_json = callbacks->enrich(WsMessage::done_with_outcome(session_id, usage_json, outcome_json, total_seconds, ttfb_seconds)).to_json();
        log::info_fmt("Server: enqueue terminal done session={} reason={} frame_len={}",
                      session_id.c_str(), llm::to_string(result.outcome.reason), done_json.size());
        auto ws_for_done = ws;
        ws->loop().submit_task([ws_for_done, done_json = std::move(done_json)]() mutable {
            if (ws_for_done && ws_for_done->alive()) ws_for_done->queue_send(std::move(done_json));
        });
    };

    try {
        if (auto resources = entry->agent->resources()) {
            if (auto runtime = resources->sub_agent_runtime()) {
                runtime->set_parent_callbacks(callbacks.get());
            }
            if (auto workflow_engine = resources->workflow_engine()) {
                workflow_engine->set_progress_callbacks(callbacks);
            }
        }
        auto& agent_loop = entry->agent->resources()->io_context()->loop();
        auto result = co_await entry->agent->run_session_async(agent_loop, *entry->session, container::String(prompt), *callbacks, std::move(options), run_guard.cancel);
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
