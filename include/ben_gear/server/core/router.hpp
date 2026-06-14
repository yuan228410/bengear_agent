#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/base/container/map.hpp"
#include "ben_gear/base/container/vector.hpp"

#include <functional>
#include <string>

namespace ben_gear::server {

namespace container = base::container;

/// HTTP 请求
struct HttpRequest {
    container::String method;
    container::String path;
    container::String version;
    container::Map<container::String, container::String> headers;
    std::string body;
    container::Map<container::String, container::String> params;
    container::Map<container::String, container::String> query;
    /// 请求关联的用户名（由 authenticate 填充）
    container::String username;
};

/// HTTP 响应
struct HttpResponse {
    int status = 200;
    container::Map<container::String, container::String> headers;
    std::string body;
    bool is_sse = false;
    bool is_websocket = false;

    static HttpResponse json(int s, const std::string& b) {
        HttpResponse r;
        r.status = s;
        r.headers["Content-Type"] = "application/json";
        r.body = b;
        return r;
    }
    static HttpResponse ok(const std::string& b = "{}") { return json(200, b); }
    static HttpResponse error(int s, const std::string& msg) {
        return json(s, "{\"error\":\"" + msg + "\"}");
    }
    static HttpResponse not_found() { return error(404, "not found"); }
};

using RouteHandler = std::function<HttpResponse(const HttpRequest&)>;

/// HTTP 路由器（支持路径参数 :id）
class Router {
public:
    void add_route(const container::String& method,
                   const container::String& path_pattern,
                   RouteHandler handler);

    RouteHandler* match(const container::String& method,
                        const container::String& path,
                        HttpRequest& request);

    void set_cors_origins(const container::Vector<container::String>& origins) {
        cors_origins_ = origins;
    }
    size_t match_count() const { return routes_.size(); }

    void apply_cors(const HttpRequest& req, HttpResponse& resp) const;

private:
    struct Route {
        container::String method;
        container::String pattern;
        container::Vector<container::String> param_names;
        RouteHandler handler;
    };
    container::Vector<Route> routes_;
    container::Vector<container::String> cors_origins_;

    bool match_path(const container::String& pattern,
                    const container::String& path,
                    container::Map<container::String, container::String>& params) const;
};

} // namespace ben_gear::server
