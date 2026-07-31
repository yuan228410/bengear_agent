#pragma once
#include <filesystem>

#include "server/core/router.hpp"
#include "server/ws/handler.hpp"
#include "server/session/pool.hpp"
#include "server/http/static_files.hpp"
#include "config/settings.hpp"
#include "net/event_loop.hpp"
#include "net/io_context.hpp"
#include "net/task.hpp"
#include "net/socket.hpp"
#include "workspace/resolver.hpp"
#include "workspace/manager.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

namespace ben_gear::server {

class Server {
public:
    explicit Server(config::Settings settings);
    ~Server();
    void run();
    void stop();

    /// 热重载配置（从 config.json 重新加载）
    /// 新连接和新会话自动使用新配置，已有会话保持原配置
    bool reload_settings();

    /// 获取当前配置（线程安全）
    config::Settings settings() const;

private:
    void setup_routes();

    /// 获取用户目录（~/.bengear/users/<username>/）
    std::filesystem::path user_dir_for(const std::string& username) const;

    /// 构建工作空间的 TierPaths
    workspace::TierPaths tier_paths_for(const std::string& username,
                                         const std::string& workspace) const;

    /// 获取工作空间关联的项目路径
    std::string project_path_for(const std::string& username,
                                       const std::string& workspace) const;

    net::Task<void> accept_loop(net::Socket listen_socket);
    net::Task<void> handle_connection(net::TcpStream stream);
    net::Task<void> handle_websocket(net::TcpStream stream, const std::string& ws_key,
                                      const std::string& origin, const std::string& username);
    net::Task<void> send_response(net::TcpStream& stream, const HttpResponse& resp);

    /// Session factory used by handle_websocket
    std::shared_ptr<SessionEntry> get_or_create_agent_session(
        const std::string& session_id, const std::string& username,
        const std::string& workspace);

    /// 共享配置指针——所有引用者通过 shared_ptr 获取当前配置
    /// reload_settings 时原子替换指针，旧引用者保持旧配置
    std::shared_ptr<config::Settings> settings_;
    mutable std::mutex settings_mutex_;  ///< 保护 settings_ 的原子替换（mutable: const 方法可锁）
    std::unique_ptr<Router> router_;
    std::unique_ptr<SessionPool> session_pool_;
    std::unique_ptr<StaticFileServer> static_files_;
    std::shared_ptr<net::IoContext> io_context_;
    std::shared_ptr<workspace::HistoryDB> history_db_;
    workspace::WorkspaceResolver workspace_resolver_;
    std::atomic<bool> running_{false};
};

} // namespace ben_gear::server
