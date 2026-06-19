#include "ben_gear/server/api/permission_api.hpp"

#include "ben_gear/base/log/logger.hpp"

#include <string>

namespace ben_gear::server {

namespace {

container::String query_string(const HttpRequest& req, std::string_view key) {
    auto it = req.query.find(container::String(key));
    if (it == req.query.end()) return container::String();
    return it->second;
}

Json parse_body_object(const HttpRequest& req, std::string& error) {
    if (req.body.empty()) return Json::object();
    try {
        auto json = Json::parse(req.body);
        if (!json.is_object()) {
            error = "request body must be a JSON object";
            return Json();
        }
        return json;
    } catch (const std::exception& e) {
        error = e.what();
        return Json();
    }
}

HttpResponse bad_request(std::string_view message) {
    return HttpResponse::json(400, Json{{"success", false}, {"error_type", "bad_request"}, {"message", std::string(message)}}.dump().to_std_string());
}

container::String require_session_id(const Json& body, const HttpRequest& req) {
    auto session_id = body.value("session_id", "");
    if (!session_id.empty()) return container::String(session_id.c_str());
    return query_string(req, "session_id");
}

container::String workspace_or_default(const Json& body, const HttpRequest& req) {
    auto workspace = body.value("workspace", "");
    if (!workspace.empty()) return container::String(workspace.c_str());
    return query_string(req, "workspace");
}

HttpResponse json_response(const Json& json) {
    auto status = 200;
    if (!json.value("success", true)) {
        auto error_type = std::string(json.value("error_type", ""));
        if (error_type == "permission_not_found" || error_type == "session_not_found") status = 404;
    }
    return HttpResponse::json(status, json.dump().to_std_string());
}

} // namespace

void register_permission_routes(Router& router, PermissionApiService& svc) {
    router.add_route("GET", "/api/permissions",
        [svc](const HttpRequest& req) {
            auto session_id = query_string(req, "session_id");
            if (session_id.empty()) return bad_request("missing session_id");
            if (!svc.list_pending) return HttpResponse::error(500, "permission list service unavailable");
            return json_response(svc.list_pending(query_string(req, "workspace"), session_id, req.username));
        });

    router.add_route("POST", "/api/permissions/:permission_id/approve",
        [svc](const HttpRequest& req) {
            std::string error;
            auto body = parse_body_object(req, error);
            if (!error.empty()) return bad_request(error);
            auto session_id = require_session_id(body, req);
            if (session_id.empty()) return bad_request("missing session_id");
            auto id_it = req.params.find(container::String("permission_id"));
            if (id_it == req.params.end() || id_it->second.empty()) return bad_request("missing permission_id");
            if (!svc.approve) return HttpResponse::error(500, "permission approve service unavailable");
            return json_response(svc.approve(workspace_or_default(body, req), session_id, req.username, id_it->second, body.value("allow_session", false)));
        });

    router.add_route("POST", "/api/permissions/:permission_id/deny",
        [svc](const HttpRequest& req) {
            std::string error;
            auto body = parse_body_object(req, error);
            if (!error.empty()) return bad_request(error);
            auto session_id = require_session_id(body, req);
            if (session_id.empty()) return bad_request("missing session_id");
            auto id_it = req.params.find(container::String("permission_id"));
            if (id_it == req.params.end() || id_it->second.empty()) return bad_request("missing permission_id");
            if (!svc.deny) return HttpResponse::error(500, "permission deny service unavailable");
            return json_response(svc.deny(workspace_or_default(body, req), session_id, req.username, id_it->second));
        });

    log::info_fmt("API: permission routes registered (3)");
}

} // namespace ben_gear::server
