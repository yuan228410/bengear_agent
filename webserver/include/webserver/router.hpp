#pragma once

#include "webserver/core.hpp"
#include "webserver/thread_pool.hpp"

#include <functional>
#include <memory>

namespace ws {

/// HTTP 请求路由处理器
using RequestHandler = std::function<HttpResponse(const HttpRequest&)>;

/// 路由表：方法 + 路径 -> 处理器
class Router {
public:
    Router() = default;

    /// 注册 GET 路由
    Router& get(std::string_view path, RequestHandler handler);

    /// 注册 POST 路由
    Router& post(std::string_view path, RequestHandler handler);

    /// 注册 PUT 路由
    Router& put(std::string_view path, RequestHandler handler);

    /// 注册 DELETE 路由
    Router& del(std::string_view path, RequestHandler handler);

    /// 注册任意方法路由
    Router& route(std::string_view method, std::string_view path, RequestHandler handler);

    /// 查找并执行路由，未找到返回 404
    [[nodiscard]] HttpResponse dispatch(const HttpRequest& request) const;

    /// 设置默认 404 处理器
    Router& not_found_handler(RequestHandler handler);

private:
    struct RouteKey {
        std::string method;
        std::string path;

        bool operator==(const RouteKey& other) const {
            return method == other.method && path == other.path;
        }
    };

    struct RouteKeyHash {
        size_t operator()(const RouteKey& key) const {
            return std::hash<std::string>()(key.method) ^ (std::hash<std::string>()(key.path) << 1);
        }
    };

    std::unordered_map<RouteKey, RequestHandler, RouteKeyHash> routes_;
    RequestHandler not_found_handler_;
};

}  // namespace ws
