#include "ben_gear/server/api/diagnostic_repair_api.hpp"

#include "ben_gear/base/log/logger.hpp"

#include <string>

namespace ben_gear::server {

namespace {

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
    return HttpResponse::json(200, json.dump().to_std_string());
}

HttpResponse bad_request(std::string_view message) {
    return HttpResponse::json(400, Json{{"success", false}, {"error_type", "bad_request"}, {"message", std::string(message)}}.dump().to_std_string());
}

container::String workspace_or_default(const Json& body, const HttpRequest& req) {
    auto workspace = body.value("workspace", "");
    if (!workspace.empty()) return container::String(workspace.c_str());
    auto it = req.query.find(container::String("workspace"));
    if (it == req.query.end()) return container::String();
    return it->second;
}

} // namespace

void register_diagnostic_repair_routes(Router& router, DiagnosticRepairApiService& svc) {
    router.add_route("POST", "/api/diagnostics/repair-plan",
        [svc](const HttpRequest& req) {
            std::string error;
            auto body = parse_body_object(req, error);
            if (!error.empty()) return bad_request(error);
            if (!svc.repair_plan) return HttpResponse::error(500, "diagnostic repair service unavailable");
            auto workspace = workspace_or_default(body, req);
            auto request = body;
            request.erase("workspace");
            auto result = svc.repair_plan(workspace, req.username, request);
            if (!result.value("success", false) && result.value("error_type", "") == "invalid_arguments") {
                return HttpResponse::json(400, result.dump().to_std_string());
            }
            return json_response(result);
        });

    router.add_route("POST", "/api/diagnostics/repair-patch-preview",
        [svc](const HttpRequest& req) {
            std::string error;
            auto body = parse_body_object(req, error);
            if (!error.empty()) return bad_request(error);
            if (!svc.repair_patch_preview) return HttpResponse::error(500, "diagnostic repair patch preview service unavailable");
            auto workspace = workspace_or_default(body, req);
            auto request = body;
            request.erase("workspace");
            auto result = svc.repair_patch_preview(workspace, req.username, request);
            if (!result.value("success", false) && result.value("error_type", "") == "invalid_arguments") {
                return HttpResponse::json(400, result.dump().to_std_string());
            }
            return json_response(result);
        });


    router.add_route("POST", "/api/diagnostics/repair-patch-draft",
        [svc](const HttpRequest& req) {
            std::string error;
            auto body = parse_body_object(req, error);
            if (!error.empty()) return bad_request(error);
            if (!svc.repair_patch_draft) return HttpResponse::error(500, "diagnostic repair patch draft service unavailable");
            auto workspace = workspace_or_default(body, req);
            auto request = body;
            request.erase("workspace");
            auto result = svc.repair_patch_draft(workspace, req.username, request);
            if (!result.value("success", false) && result.value("error_type", "") == "invalid_arguments") {
                return HttpResponse::json(400, result.dump().to_std_string());
            }
            return json_response(result);
        });

    router.add_route("POST", "/api/diagnostics/repair-workflow",
        [svc](const HttpRequest& req) {
            std::string error;
            auto body = parse_body_object(req, error);
            if (!error.empty()) return bad_request(error);
            if (!svc.repair_workflow) return HttpResponse::error(500, "diagnostic repair workflow service unavailable");
            auto workspace = workspace_or_default(body, req);
            auto request = body;
            request.erase("workspace");
            auto result = svc.repair_workflow(workspace, req.username, request);
            if (!result.value("success", false) && result.value("error_type", "") == "invalid_arguments") {
                return HttpResponse::json(400, result.dump().to_std_string());
            }
            return json_response(result);
        });

    log::info_fmt("API: diagnostic repair routes registered (4)");
}

} // namespace ben_gear::server
