#include "ben_gear/server/api/file_api.hpp"
#include "ben_gear/base/log/logger.hpp"

#include <string>

namespace ben_gear::server {

static std::string escape_json(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c;
        }
    }
    return out;
}

void register_file_routes(Router& router, FileService& svc) {
    router.add_route("GET", "/api/files/home",
        [svc](const HttpRequest& req) {
            auto home = svc.home_directory ? svc.home_directory(req.username) : container::String("/");
            std::string json = "{\"path\":\"" + escape_json(std::string(home.c_str())) + "\"}";
            return HttpResponse::ok(json);
        });

    router.add_route("GET", "/api/files/list",
        [svc](const HttpRequest& req) {
            container::String query_path;
            auto it = req.query.find("path");
            if (it != req.query.end()) {
                query_path = it->second;
            }

            auto entries = svc.list_files(query_path, req.username);

            std::string json = "[";
            bool first = true;
            for (const auto& e : entries) {
                if (!first) json += ",";
                json += "{";
                json += "\"name\":\"" + escape_json(std::string(e.name.c_str())) + "\",";
                json += "\"type\":\"" + std::string(e.type.c_str()) + "\",";
                json += "\"size\":" + std::to_string(e.size) + ",";
                json += "\"modified\":\"" + std::string(e.modified.c_str()) + "\"";
                json += "}";
                first = false;
            }
            json += "]";
            return HttpResponse::ok(json);
        });

    log::info_fmt("API: file routes registered (2)");
}

} // namespace ben_gear::server
