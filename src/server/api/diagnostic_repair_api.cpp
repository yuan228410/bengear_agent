#include "server/api/diagnostic_repair_api.hpp"

#include "base/log/logger.hpp"

#include <string>
#include "server/api/internal/api_util.hpp"

namespace ben_gear::server {

namespace {

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
