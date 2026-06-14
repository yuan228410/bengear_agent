#pragma once

#include "ben_gear/server/core/router.hpp"
#include "ben_gear/server/ws/handler.hpp"
#include "ben_gear/server/session/pool.hpp"
#include "ben_gear/server/http/static_files.hpp"
#include "ben_gear/server/callback/server_callbacks.hpp"
#include "ben_gear/config/settings.hpp"
#include "ben_gear/base/net/event_loop.hpp"
#include "ben_gear/base/net/io_context.hpp"
#include "ben_gear/base/net/task.hpp"
#include "ben_gear/base/net/socket.hpp"
#include "ben_gear/workspace/manager.hpp"

#include <atomic>
#include <memory>
#include <string>

namespace ben_gear::server {

class Server {
public:
    explicit Server(config::Settings settings);
    ~Server();
    void run();
    void stop();
    const config::Settings& settings() const { return settings_; }

private:
    void setup_routes();

    /// 获取用户目录（~/.bengear/users/<username>/）
    std::filesystem::path user_dir_for(const container::String& username) const;

    /// 构建工作空间的 TierPaths
    workspace::TierPaths tier_paths_for(const container::String& username,
                                         const container::String& workspace) const;

    /// 获取工作空间关联的项目路径
    container::String project_path_for(const container::String& username,
                                       const container::String& workspace) const;

    net::Task<void> accept_loop(net::Socket listen_socket);
    net::Task<void> handle_connection(net::TcpStream stream);
    net::Task<void> handle_websocket(net::TcpStream stream, const std::string& ws_key,
                                      const std::string& origin, const container::String& username);
    net::Task<void> send_response(net::TcpStream& stream, const HttpResponse& resp);

    void on_ws_message(std::shared_ptr<WsHandler> ws, const container::String& username,
                       std::string_view message);
    net::Task<void> handle_ws_chat(std::shared_ptr<WsHandler> ws,
                                    std::shared_ptr<ServerCallbacks> callbacks,
                                    container::String session_id, container::String prompt,
                                    std::shared_ptr<SessionEntry> entry);
    net::Task<void> handle_ws_plan_start(std::shared_ptr<WsHandler> ws,
                                          container::String session_id,
                                          container::String prompt,
                                          container::String note,
                                          std::shared_ptr<SessionEntry> entry);
    net::Task<void> handle_ws_plan_chat(std::shared_ptr<WsHandler> ws,
                                         container::String session_id,
                                         container::String note,
                                         std::shared_ptr<SessionEntry> entry);
    net::Task<void> handle_ws_plan_update_items(std::shared_ptr<WsHandler> ws,
                                                 container::String session_id,
                                                 container::Vector<orchestration::PlanItem> items,
                                                 std::shared_ptr<SessionEntry> entry);
    net::Task<void> handle_ws_plan_select_option(std::shared_ptr<WsHandler> ws,
                                                 container::String session_id,
                                                 container::String option_id,
                                                 std::shared_ptr<SessionEntry> entry);
    net::Task<void> handle_ws_plan_confirm(std::shared_ptr<WsHandler> ws,
                                            std::shared_ptr<ServerCallbacks> callbacks,
                                            container::String session_id,
                                            int revision,
                                            bool has_items,
                                            container::Vector<orchestration::PlanItem> items,
                                            std::shared_ptr<SessionEntry> entry);
    net::Task<void> handle_ws_plan_cancel(std::shared_ptr<WsHandler> ws,
                                           container::String session_id,
                                           std::shared_ptr<SessionEntry> entry);
    net::Task<void> handle_ws_todo_update(std::shared_ptr<WsHandler> ws,
                                           orchestration::TodoItem item,
                                           std::shared_ptr<SessionEntry> entry);

    std::shared_ptr<SessionEntry> get_or_create_agent_session(
        const container::String& session_id, const container::String& username,
        const container::String& workspace);

    config::Settings settings_;
    std::unique_ptr<Router> router_;
    std::unique_ptr<SessionPool> session_pool_;
    std::unique_ptr<StaticFileServer> static_files_;
    std::shared_ptr<net::IoContext> io_context_;
    std::atomic<bool> running_{false};
};

} // namespace ben_gear::server
