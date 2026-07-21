#pragma once

#include <memory>
#include <unordered_map>
#include <string>
#include <vector>
#include "base/utils/json.hpp"

#include <functional>

namespace ben_gear::server {


/// HTTP 请求
struct HttpRequest {
    std::string method;
    std::string path;
    std::string version;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
    std::unordered_map<std::string, std::string> params;
    std::unordered_map<std::string, std::string> query;
    /// 请求关联的用户名（由 authenticate 填充）
    std::string username;
};

/// HTTP 响应
struct HttpResponse {
    int status = 200;
    std::unordered_map<std::string, std::string> headers;
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
        base::json::Json body;
        body["error"] = msg;
        auto dumped = body.dump();
        return json(s, std::string(dumped.data(), dumped.size()));
    }
    static HttpResponse not_found() { return error(404, "not found"); }
};

using RouteHandler = std::function<HttpResponse(const HttpRequest&)>;

/// HTTP 路由器（基于 Trie 前缀树，O(k) 匹配）
class Router {
public:
    Router();
    ~Router();

    void add_route(const std::string& method,
                   const std::string& path_pattern,
                   RouteHandler handler);

    RouteHandler* match(const std::string& method,
                        const std::string& path,
                        HttpRequest& request);

    void set_cors_origins(const std::vector<std::string>& origins) {
        cors_origins_ = origins;
    }
    size_t match_count() const { return route_count_; }

    void apply_cors(const HttpRequest& req, HttpResponse& resp) const;

private:
    struct TrieNode {
        std::unordered_map<std::string, std::unique_ptr<TrieNode>> children;
        std::unique_ptr<TrieNode> param_child;
        std::string param_name;
        RouteHandler handler;
        bool has_handler = false;
    };

    TrieNode root_;
    size_t route_count_ = 0;
    std::vector<std::string> cors_origins_;

    static std::vector<std::string> split_path(const std::string& path);
    static std::string url_decode_segment(const std::string& input);
};

} // namespace ben_gear::server
