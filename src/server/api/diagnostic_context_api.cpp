#include "server/api/diagnostic_context_api.hpp"

#include "base/log/logger.hpp"

#include <string>
#include "server/api/internal/api_util.hpp"

namespace ben_gear::server {

namespace {

} // namespace

void register_diagnostic_context_routes(Router& router, DiagnosticContextApiService& svc) {
    router.add_route("POST", "/api/diagnostics/repair-context",
        [svc](const HttpRequest& req) {
            std::string error;
            auto body = parse_body_object(req, error);
            if (!error.empty()) return bad_request(error);
            if (!svc.repair_context) return HttpResponse::error(500, "diagnostic context service unavailable");
            auto workspace = workspace_or_default(body, req);
            auto request = body;
            request.erase("workspace");
            auto result = svc.repair_context(workspace, req.username, request);
            if (!result.value("success", false) && result.value("error_type", "") == "invalid_arguments") {
                return HttpResponse::json(400, result.dump().to_std_string());
            }
            return json_response(result);
        });

    log::info_fmt("API: diagnostic context routes registered (1)");
}

} // namespace ben_gear::server
