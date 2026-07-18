#include "server/core/router.hpp"

#include "base/log/logger.hpp"

#include <cctype>
#include <cstdlib>
#include <sstream>
#include <string_view>

namespace ben_gear::server {

Router::Router() : root_() {}
Router::~Router() = default;

std::vector<std::string> Router::split_path(const std::string& path) {
    std::vector<std::string> parts;
    size_t start = 0;
    if (!path.empty() && path[0] == '/') start = 1;
    size_t pos = start;
    while (pos < path.size()) {
        auto slash = path.find('/', pos);
        if (slash == std::string::npos) {
            parts.push_back(path.substr(pos));
            break;
        }
        parts.push_back(path.substr(pos, slash - pos));
        pos = slash + 1;
    }
    return parts;
}

std::string Router::url_decode_segment(const std::string& input) {
    std::string_view sv(input.data(), input.size());
    std::string out;
    out.reserve(sv.size());
    for (size_t i = 0; i < sv.size(); ++i) {
        if (sv[i] == '%' && i + 2 < sv.size()) {
            char buf[3] = {static_cast<char>(sv[i + 1]), static_cast<char>(sv[i + 2]), '\0'};
            char* end = nullptr;
            long val = std::strtol(buf, &end, 16);
            if (end == buf + 2) {
                out.push_back(static_cast<char>(val));
                i += 2;
                continue;
            }
        }
        out.push_back(sv[i]);
    }
    return out;
}

void Router::add_route(const std::string& method,
                       const std::string& path_pattern,
                       RouteHandler handler) {
    // 第一层：以 method 为 key
    auto it = root_.children.find(method);
    TrieNode* node;
    if (it != root_.children.end()) {
        node = it->second.get();
    } else {
        auto new_node = std::make_unique<TrieNode>();
        node = new_node.get();
        root_.children[method] = std::move(new_node);
    }

    // 后续层：按路径段插入
    auto parts = split_path(path_pattern);
    for (const auto& seg : parts) {
        if (!seg.empty() && seg[0] == ':') {
            std::string param_name = seg.substr(1);
            if (!node->param_child) {
                node->param_child = std::make_unique<TrieNode>();
                node->param_child->param_name = std::move(param_name);
            }
            node = node->param_child.get();
        } else {
            auto child_it = node->children.find(seg);
            if (child_it != node->children.end()) {
                node = child_it->second.get();
            } else {
                node->children[seg] = std::make_unique<TrieNode>();
                node = node->children[seg].get();
            }
        }
    }

    if (node->has_handler) {
        log::warn_fmt("Router: duplicate route {} {}, overwriting", method.c_str(), path_pattern.c_str());
    }
    node->handler = std::move(handler);
    node->has_handler = true;
    ++route_count_;
    log::debug_fmt("Router: registered {} {}", method.c_str(), path_pattern.c_str());
}

RouteHandler* Router::match(const std::string& method,
                            const std::string& path,
                            HttpRequest& request) {
    // 先找 method 节点
    auto method_it = root_.children.find(method);
    if (method_it == root_.children.end()) return nullptr;

    auto* node = method_it->second.get();
    if (!node) return nullptr;

    auto parts = split_path(path);
    request.params.clear();

    for (const auto& seg : parts) {
        // 先尝试精确匹配
        auto it = node->children.find(seg);
        if (it != node->children.end()) {
            node = it->second.get();
        } else if (node->param_child) {
            // 尝试路径参数匹配
            request.params[node->param_child->param_name] = url_decode_segment(seg);
            node = node->param_child.get();
        } else {
            return nullptr;
        }
    }

    if (node && node->has_handler) {
        return &node->handler;
    }
    return nullptr;
}

void Router::apply_cors(const HttpRequest& req, HttpResponse& resp) const {
    if (cors_origins_.empty()) return;

    std::string origin;
    if (auto it = req.headers.find("origin"); it != req.headers.end()) {
        origin = it->second.c_str();
    }

    bool allow = false;
    for (const auto& o : cors_origins_) {
        if (o == "*" || o == origin) { allow = true; break; }
    }

    if (allow) {
        resp.headers["Access-Control-Allow-Origin"] =
            cors_origins_[0] == "*" ? std::string("*")
                                     : origin;
        resp.headers["Access-Control-Allow-Methods"] = "GET, POST, PUT, DELETE, OPTIONS";
        resp.headers["Access-Control-Allow-Headers"] = "Content-Type, Authorization, X-Username";
        resp.headers["Access-Control-Max-Age"] = "86400";
    }
}

} // namespace ben_gear::server
