#include "webserver/router.hpp"
#include "webserver/logging.hpp"

namespace ws {

// ============ HttpRouter ============

void HttpRouter::add_route(Method method, const std::string& path, RouteHandler handler) {
    routes_.emplace_back(Route{method, path, std::move(handler)});
    log::info_fmt("Router: added route {} {}", method_to_string(method), path);
}

void HttpRouter::add_static_dir(const std::string& url_prefix, const std::string& dir_path) {
    static_dirs_.emplace_back(StaticDir{url_prefix, dir_path});
    log::info_fmt("Router: static dir {} -> {}", url_prefix, dir_path);
}

RouteHandler HttpRouter::find_handler(const HttpRequest& req) const {
    // 先尝试精确匹配
    for (const auto& route : routes_) {
        if (route.method == req.method && route.path == req.path) {
            log::debug_fmt("Router: matched route {} {}", method_to_string(route.method), route.path);
            return route.handler;
        }
    }

    // 尝试通配符匹配（尾随 *）
    for (const auto& route : routes_) {
        if (route.method == req.method && route.path.size() > 2 &&
            route.path.back() == '*') {
            std::string prefix = route.path.substr(0, route.path.size() - 1);
            if (req.path.compare(0, prefix.size(), prefix) == 0) {
                log::debug_fmt("Router: matched wildcard route {} {}", 
                               method_to_string(route.method), route.path);
                return route.handler;
            }
        }
    }

    log::debug_fmt("Router: no handler found for {} {}", 
                   method_to_string(req.method), req.path);
    return nullptr;
}

bool HttpRouter::try_serve_static(const HttpRequest& req, HttpResponse& resp,
                                   const std::string& doc_root) const {
    // 先检查注册的静态目录
    for (const auto& sd : static_dirs_) {
        if (req.path.compare(0, sd.url_prefix.size(), sd.url_prefix) == 0) {
            std::string relative_path = req.path.substr(sd.url_prefix.size());
            if (relative_path.empty() || relative_path.front() != '/') {
                relative_path = "/" + relative_path;
            }
            std::string full_path = sd.dir_path + relative_path;
            return serve_file(full_path, resp);
        }
    }

    // 默认从 doc_root 服务
    std::string file_path = doc_root + req.path;
    if (file_path.back() == '/') {
        file_path += "index.html";
    }

    return serve_file(file_path, resp);
}

bool HttpRouter::serve_file(const std::string& file_path, HttpResponse& resp) {
    std::error_code ec;
    auto file_size = std::filesystem::file_size(file_path, ec);
    if (ec) {
        return false;
    }

    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }

    // 读取文件内容
    std::string content(file_size, '\0');
    file.read(content.data(), file_size);

    if (!file) {
        return false;
    }

    // 设置 Content-Type
    auto ext = std::filesystem::path(file_path).extension().string();
    static const std::unordered_map<std::string, std::string> mime_types = {
        {".html", "text/html"},
        {".htm",  "text/html"},
        {".css",  "text/css"},
        {".js",   "application/javascript"},
        {".json", "application/json"},
        {".png",  "image/png"},
        {".jpg",  "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".gif",  "image/gif"},
        {".svg",  "image/svg+xml"},
        {".ico",  "image/x-icon"},
        {".txt",  "text/plain"},
        {".xml",  "application/xml"},
        {".pdf",  "application/pdf"},
        {".zip",  "application/zip"},
        {".mp3",  "audio/mpeg"},
        {".mp4",  "video/mp4"},
        {".woff", "font/woff"},
        {".woff2","font/woff2"},
    };

    auto it = mime_types.find(ext);
    resp.headers["Content-Type"] = (it != mime_types.end()) ? it->second : "application/octet-stream";
    resp.body = std::move(content);
    resp.status_code = 200;
    resp.status_message = "OK";

    log::debug_fmt("Router: served static file {}", file_path);
    return true;
}

std::string HttpRouter::method_to_string(Method method) {
    switch (method) {
        case Method::GET:     return "GET";
        case Method::POST:    return "POST";
        case Method::PUT:     return "PUT";
        case Method::DELETE:  return "DELETE";
        case Method::HEAD:    return "HEAD";
        case Method::OPTIONS: return "OPTIONS";
        case Method::PATCH:   return "PATCH";
        default:              return "UNKNOWN";
    }
}

Method HttpRouter::string_to_method(const std::string& s) {
    if (s == "GET")     return Method::GET;
    if (s == "POST")    return Method::POST;
    if (s == "PUT")     return Method::PUT;
    if (s == "DELETE")  return Method::DELETE;
    if (s == "HEAD")    return Method::HEAD;
    if (s == "OPTIONS") return Method::OPTIONS;
    if (s == "PATCH")   return Method::PATCH;
    return Method::UNKNOWN;
}

}  // namespace ws
