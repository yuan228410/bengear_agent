#include "server/api/patch_api.hpp"

#include "base/log/logger.hpp"

#include <string>
#include "server/api/internal/api_util.hpp"

namespace ben_gear::server {

namespace {

} // namespace

void register_patch_routes(Router& router, PatchApiService& svc) {
    router.add_route("POST", "/api/patch/preview",
        [svc](const HttpRequest& req) {
            std::string error;
            auto body = parse_body_object(req, error);
            if (!error.empty()) return bad_request(error);
            auto session_id = require_session_id(body, req);
            if (session_id.empty()) return bad_request("missing session_id");
            auto unified_diff = std::string(body.value("unified_diff", ""));
            if (unified_diff.empty()) return bad_request("missing unified_diff");
            if (!svc.preview_patch) return HttpResponse::error(500, "patch preview service unavailable");
            return json_response(svc.preview_patch(workspace_or_default(body, req), session_id, req.username, unified_diff));
        });

    router.add_route("POST", "/api/patch/apply",
        [svc](const HttpRequest& req) {
            std::string error;
            auto body = parse_body_object(req, error);
            if (!error.empty()) return bad_request(error);
            auto session_id = require_session_id(body, req);
            if (session_id.empty()) return bad_request("missing session_id");
            auto unified_diff = std::string(body.value("unified_diff", ""));
            if (unified_diff.empty()) return bad_request("missing unified_diff");
            auto description = std::string(body.value("description", ""));
            if (!svc.apply_patch) return HttpResponse::error(500, "patch apply service unavailable");
            auto workspace = workspace_or_default(body, req);
            return json_response(svc.apply_patch(workspace, session_id, req.username, unified_diff, description));
        });

    router.add_route("POST", "/api/patch/safe-change",
        [svc](const HttpRequest& req) {
            std::string error;
            auto body = parse_body_object(req, error);
            if (!error.empty()) return bad_request(error);
            auto session_id = require_session_id(body, req);
            if (session_id.empty()) return bad_request("missing session_id");
            auto unified_diff = std::string(body.value("unified_diff", ""));
            if (unified_diff.empty()) return bad_request("missing unified_diff");
            auto description = std::string(body.value("description", ""));
            auto test_command = std::string(body.value("test_command", ""));
            auto test_cwd = std::string(body.value("test_cwd", "."));
            auto timeout_seconds = body.value("test_timeout_seconds", 120);
            auto max_output_bytes = body.value("test_max_output_bytes", 60000);
            if (!svc.safe_code_change) return HttpResponse::error(500, "safe code change service unavailable");
            return json_response(svc.safe_code_change(workspace_or_default(body, req),
                                                      session_id,
                                                      req.username,
                                                      unified_diff,
                                                      description,
                                                      test_command,
                                                      test_cwd,
                                                      timeout_seconds,
                                                      max_output_bytes));
        });

    router.add_route("GET", "/api/changes",
        [svc](const HttpRequest& req) {
            auto session_id = query_string(req, "session_id");
            if (session_id.empty()) return bad_request("missing session_id");
            if (!svc.list_changes) return HttpResponse::error(500, "change list service unavailable");
            return json_response(svc.list_changes(query_string(req, "workspace"), session_id, req.username));
        });

    router.add_route("GET", "/api/changes/:change_id",
        [svc](const HttpRequest& req) {
            auto session_id = query_string(req, "session_id");
            if (session_id.empty()) return bad_request("missing session_id");
            auto id_it = req.params.find(container::String("change_id"));
            if (id_it == req.params.end() || id_it->second.empty()) return bad_request("missing change_id");
            if (!svc.read_change) return HttpResponse::error(500, "change read service unavailable");
            return json_response(svc.read_change(query_string(req, "workspace"), session_id, req.username, id_it->second));
        });

    router.add_route("POST", "/api/changes/:change_id/revert",
        [svc](const HttpRequest& req) {
            std::string error;
            auto body = parse_body_object(req, error);
            if (!error.empty()) return bad_request(error);
            auto session_id = require_session_id(body, req);
            if (session_id.empty()) return bad_request("missing session_id");
            auto id_it = req.params.find(container::String("change_id"));
            if (id_it == req.params.end() || id_it->second.empty()) return bad_request("missing change_id");
            auto force = body.value("force", false);
            if (!svc.revert_change) return HttpResponse::error(500, "change revert service unavailable");
            auto workspace = workspace_or_default(body, req);
            return json_response(svc.revert_change(workspace, session_id, req.username, id_it->second, force));
        });

    log::info_fmt("API: patch routes registered (6)");
}

} // namespace ben_gear::server
