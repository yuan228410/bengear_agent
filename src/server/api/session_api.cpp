#include "ben_gear/server/api/session_api.hpp"
#include "ben_gear/base/log/logger.hpp"
#include <string>

namespace ben_gear::server {

void register_session_routes(Router& router, SessionService& svc) {
    router.add_route("GET", "/api/sessions",
        [svc](const HttpRequest& req) {
            auto sessions = svc.list_sessions(container::String(), req.username);
            std::string json = "[";
            bool first = true;
            for (const auto& s : sessions) { if (!first) json += ","; json += s.dump(); first = false; }
            json += "]";
            return HttpResponse::ok(json);
        });

    router.add_route("POST", "/api/sessions",
        [svc](const HttpRequest& req) {
            try {
                // 支持从 body 传 workspace 和 name
                auto ws = container::String();
                auto name = container::String("New Session");
                if (!req.body.empty()) {
                    auto j = Json::parse(req.body);
                    if (j.contains("workspace")) ws = container::String(j["workspace"].get<std::string>().c_str());
                    if (j.contains("name")) name = container::String(j["name"].get<std::string>().c_str());
                }
                auto sid = svc.create_session(name, ws, req.username);
                return HttpResponse::ok("{\"session_id\":\"" + std::string(sid.c_str()) + "\"}");
            } catch (const std::exception& e) { return HttpResponse::error(500, e.what()); }
        });

    router.add_route("DELETE", "/api/sessions/:id",
        [svc](const HttpRequest& req) {
            auto it = req.params.find("id");
            if (it == req.params.end()) return HttpResponse::error(400, "missing id");
            // 从 query 或 body 获取 workspace
            container::String ws;
            auto ws_it = req.query.find("workspace");
            if (ws_it != req.query.end()) ws = ws_it->second;
            return svc.delete_session(it->second, ws, req.username)
                ? HttpResponse::ok("{\"ok\":true}") : HttpResponse::error(404, "not found");
        });

    router.add_route("PUT", "/api/sessions/:id",
        [svc](const HttpRequest& req) {
            auto it = req.params.find("id");
            if (it == req.params.end()) return HttpResponse::error(400, "missing id");
            try {
                auto j = Json::parse(req.body);
                auto name = j.value("name", "");
                if (name.empty()) return HttpResponse::error(400, "missing name");
                container::String ws;
                auto ws_it = req.query.find("workspace");
                if (ws_it != req.query.end()) ws = ws_it->second;
                return svc.rename_session(it->second, container::String(name.c_str()), ws, req.username)
                    ? HttpResponse::ok("{\"ok\":true}") : HttpResponse::error(404, "not found");
            } catch (const std::exception& e) { return HttpResponse::error(500, e.what()); }
        });

    router.add_route("GET", "/api/sessions/:id/history",
        [svc](const HttpRequest& req) {
            auto it = req.params.find("id");
            if (it == req.params.end()) return HttpResponse::error(400, "missing id");
            auto ws_it = req.query.find("workspace");
            auto ws = ws_it != req.query.end() ? container::String(ws_it->second.c_str()) : container::String();
            auto msgs = svc.load_history(it->second, ws, 100, req.username);
            std::string json = "[";
            bool first = true;
            for (const auto& m : msgs) { if (!first) json += ","; json += m.dump(); first = false; }
            json += "]";
            return HttpResponse::ok(json);
        });

    router.add_route("GET", "/api/workspaces/:name/sessions",
        [svc](const HttpRequest& req) {
            auto it = req.params.find("name");
            if (it == req.params.end()) return HttpResponse::error(400, "missing workspace name");
            auto sessions = svc.list_sessions_by_workspace(it->second, req.username);
            std::string json = "[";
            bool first = true;
            for (const auto& s : sessions) { if (!first) json += ","; json += s.dump(); first = false; }
            json += "]";
            return HttpResponse::ok(json);
        });

    log::info_fmt("API: session routes registered (6)");
}

} // namespace ben_gear::server
