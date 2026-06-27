#include "ben_gear/server/api/patch_api.hpp"

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

HttpResponse json_response(const Json& json) {
    auto status = 200;
    if (!json.value("success", true)) {
        auto error_type = std::string(json.value("error_type", ""));
        if (error_type == "change_not_found") status = 404;
    }
    return HttpResponse::json(status, json.dump().to_std_string());
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
