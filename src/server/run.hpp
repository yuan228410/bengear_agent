#pragma once

// 轻量级 Server 入口 — CLI 只需 include 此头文件即可启动 Server，
// 无需引入 server/core/server.hpp 的全部内部依赖。

namespace ben_gear::config { struct Settings; }

namespace ben_gear::server {

/// 启动 HTTP/WebSocket Server，阻塞直到 Server 停止
void run_server(const config::Settings& settings);

} // namespace ben_gear::server
