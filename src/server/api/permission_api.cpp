#include "server/api/permission_api.hpp"

#include "base/log/logger.hpp"

#include <string>
#include "server/api/internal/api_util.hpp"

namespace ben_gear::server {

namespace {

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
            auto id_it = req.params.find(std::string("permission_id"));
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
            auto id_it = req.params.find(std::string("permission_id"));
            if (id_it == req.params.end() || id_it->second.empty()) return bad_request("missing permission_id");
            if (!svc.deny) return HttpResponse::error(500, "permission deny service unavailable");
            return json_response(svc.deny(workspace_or_default(body, req), session_id, req.username, id_it->second));
        });

    log::info_fmt("API: permission routes registered (3)");
}

} // namespace ben_gear::server
