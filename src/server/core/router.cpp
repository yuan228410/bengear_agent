#include "ben_gear/server/core/router.hpp"

#include "ben_gear/base/log/logger.hpp"

#include <sstream>

namespace ben_gear::server {

void Router::add_route(const container::String& method,
                       const container::String& path_pattern,
                       RouteHandler handler) {
    Route route;
    route.method = method;
    route.pattern = path_pattern;
    route.handler = std::move(handler);

    // 提取路径参数名（如 :id）
    std::string_view view(path_pattern.data(), path_pattern.size());
    size_t pos = 0;
    while (pos < view.size()) {
        auto colon = view.find(':', pos);
        if (colon == std::string_view::npos) break;
        auto end = colon + 1;
        while (end < view.size() && (std::isalnum(view[end]) || view[end] == '_'))
            ++end;
        route.param_names.push_back(
            container::String(view.substr(colon + 1, end - colon - 1)));
        pos = end;
    }

    routes_.push_back(std::move(route));
    log::debug_fmt("Router: registered {} {}", method.c_str(), path_pattern.c_str());
}

RouteHandler* Router::match(const container::String& method,
                            const container::String& path,
                            HttpRequest& request) {
    for (auto& route : routes_) {
        if (route.method != method) continue;
        container::Map<container::String, container::String> params;
        if (match_path(route.pattern, path, params)) {
            request.params = std::move(params);
            return &route.handler;
        }
    }
    return nullptr;
}

bool Router::match_path(const container::String& pattern,
                        const container::String& path,
                        container::Map<container::String, container::String>& params) const {
    // 按 / 分割后逐段匹配
    auto split = [](const container::String& s) {
        container::Vector<container::String> parts;
        size_t start = 0;
        // 跳过前导 /
        if (!s.empty() && s[0] == '/') start = 1;
        size_t pos = start;
        while (pos < s.size()) {
            auto slash = s.find('/', pos);
            if (slash == container::String::npos) {
                parts.push_back(s.substr(pos));
                break;
            }
            parts.push_back(s.substr(pos, slash - pos));
            pos = slash + 1;
        }
        return parts;
    };

    auto pattern_parts = split(pattern);
    auto path_parts = split(path);

    if (pattern_parts.size() != path_parts.size()) return false;

    for (size_t i = 0; i < pattern_parts.size(); ++i) {
        const auto& pp = pattern_parts[i];
        const auto& hp = path_parts[i];
        if (!pp.empty() && pp[0] == ':') {
            // 路径参数
            params[pp.substr(1)] = hp;
        } else if (pp != hp) {
            return false;
        }
    }
    return true;
}

void Router::apply_cors(const HttpRequest& req, HttpResponse& resp) const {
    if (cors_origins_.empty()) return;

    std::string origin;
    if (auto it = req.headers.find("origin"); it != req.headers.end()) {
        origin = it->second.c_str();
    }

    bool allow = false;
    for (const auto& o : cors_origins_) {
        if (o == "*" || o.c_str() == origin) { allow = true; break; }
    }

    if (allow) {
        resp.headers["Access-Control-Allow-Origin"] =
            cors_origins_[0] == "*" ? container::String("*")
                                     : container::String(origin.c_str());
        resp.headers["Access-Control-Allow-Methods"] = "GET, POST, PUT, DELETE, OPTIONS";
        resp.headers["Access-Control-Allow-Headers"] = "Content-Type, Authorization, X-Username";
        resp.headers["Access-Control-Max-Age"] = "86400";
    }
}

} // namespace ben_gear::server
