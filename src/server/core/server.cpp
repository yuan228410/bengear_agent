#include "server/core/server.hpp"

#include "base/log/logger.hpp"
#include "base/platform/platform.hpp"
#include "orchestration/serializer.hpp"
#include "server/api/handlers.hpp"
#include "server/composition/basic_api_composition.hpp"
#include "server/composition/command_api_composition.hpp"
#include "server/composition/server_composition.hpp"
#include "server/ws/protocol.hpp"
#include "server/ws/session_message_dispatcher.hpp"
#include "server/ws/ws_session_manager.hpp"
#include "workspace/history_db.hpp"
#include "workspace/manager.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace ben_gear::server {

namespace composition_alias = ben_gear::server::composition;

Server::Server(config::Settings settings)
    : settings_(std::move(settings)),
      router_(std::make_unique<Router>()),
      session_pool_(std::make_unique<SessionPool>(settings_.server.agent_pool_max_size)),
      static_files_(std::make_unique<StaticFileServer>(settings_.server.static_dir)),
      io_context_(std::make_shared<net::IoContext>("server")),
      workspace_resolver_(application::WorkspaceResolverConfig{
          ben_gear::support::data_directory(),
          settings_.workspace_name.empty() ? std::string("default") : settings_.workspace_name,
          settings_.workspace.string()}) {
    setup_routes();
    log::info_fmt("Server: initialized on {}:{}", settings_.server.host.c_str(), settings_.server.port);
}

Server::~Server() { stop(); }

std::filesystem::path Server::user_dir_for(const std::string& username) const {
    return workspace_resolver_.user_dir_for(username);
}

workspace::TierPaths Server::tier_paths_for(const std::string& username,
                                             const std::string& workspace) const {
    return workspace_resolver_.tier_paths_for(username, workspace);
}

std::string Server::project_path_for(const std::string& username,
                                           const std::string& workspace) const {
    return workspace_resolver_.project_path_for(username, workspace);
}

void Server::setup_routes() {
    auto basic_api_context = composition_alias::BasicApiCompositionContext{settings_, workspace_resolver_, *session_pool_};
    auto session_svc = composition_alias::make_session_api_service(basic_api_context);
    auto config_svc = composition_alias::make_config_api_service(basic_api_context);
    auto ws_svc = composition_alias::make_workspace_api_service(basic_api_context);
    auto mcp_svc = composition_alias::make_mcp_api_service();
    auto file_svc = composition_alias::make_file_api_service();

    register_api_routes(*router_, session_svc, config_svc, ws_svc, mcp_svc, file_svc);

    std::vector<std::string> origins;
    if (!settings_.server.cors_origins.empty()) origins = settings_.server.cors_origins;
    else origins.push_back(std::string("*"));
    router_->set_cors_origins(origins);
    log::info_fmt("Server: {} total routes", router_->match_count());
}

net::Task<void> Server::handle_websocket(net::TcpStream stream, const std::string& ws_key,
                                          const std::string& origin, const std::string& username) {
    auto ws = std::make_shared<WsHandler>(std::move(stream), ws_key);
    try { co_await ws->handshake(origin); }
    catch (const std::exception& e) { log::error_fmt("Server: WS handshake failed: {}", e.what()); co_return; }
    log::info_fmt("Server: WS connected user={}", username.c_str());
    try {
        auto user_dir = user_dir_for(username);
        auto ws_manager = workspace::WorkspaceManager(user_dir);
        std::string ws_name;
        auto all_ws = ws_manager.list_all();
        if (!all_ws.empty()) {
            ws_name = all_ws[0].name;
        } else {
            // 新用户：自动创建 default workspace 和一个默认会话
            ws_name = std::string("default");
            ws_manager.create(ws_name, {});
            log::info_fmt("Server: created default workspace for new user={}", username.c_str());
        }

        auto db_path = user_dir_for(username) / "history.db";
        workspace::HistoryDB db(db_path);
        auto existing = db.list_sessions(ws_name);
        std::string session_id;
        if (!existing.empty()) {
            session_id = existing[0].value("session_id", "");
        }

        Json cfg;
        cfg["model"] = settings_.model;
        cfg["provider"] = provider_name(settings_.provider);
        cfg["workspace"] = ws_name;
        auto connected = WsMessage::connected(session_id, cfg.dump());
        connected.strings[std::string("workspace")] = ws_name;
        log::info_fmt("Server: WS init user={} workspace={} session={} existing_sessions={}",
                      username.c_str(), ws_name.c_str(), session_id.c_str(), existing.size());
        co_await ws->send_text(connected.to_json());
        if (!session_id.empty()) {
            auto entry = get_or_create_agent_session(session_id, username, ws_name);
            auto plan_payload = orchestration::to_json_string(entry->plan_manager.draft());
            auto plan_msg = WsMessage::plan_state(session_id, std::string(plan_payload.data(), plan_payload.size()));
            plan_msg.strings[std::string("workspace")] = ws_name;
            co_await ws->send_text(plan_msg.to_json());
            auto todo_payload = orchestration::to_json_string(entry->todo_manager.state());
            auto todo_msg = WsMessage::todo_state(session_id, std::string(todo_payload.data(), todo_payload.size()));
            todo_msg.strings[std::string("workspace")] = ws_name;
            co_await ws->send_text(todo_msg.to_json());
        }
    } catch (const std::exception& e) { log::error_fmt("Server: WS init send failed: {}", e.what()); }
    WsSessionManager session_mgr(settings_, *session_pool_, workspace_resolver_);
    co_await session_mgr.run_ws(ws, username);
}

std::shared_ptr<SessionEntry> Server::get_or_create_agent_session(
    const std::string& session_id, const std::string& username, const std::string& workspace) {
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
