#include "ben_gear/server/core/server.hpp"

#include "ben_gear/server/auth/auth.hpp"
#include "ben_gear/server/http/parser.hpp"
#include "ben_gear/server/ws/protocol.hpp"

#include "ben_gear/base/log/logger.hpp"

#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace ben_gear::server {

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

} // namespace ben_gear::server
