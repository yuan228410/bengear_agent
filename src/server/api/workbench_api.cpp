#include "server/api/workbench_api.hpp"

#include "base/log/logger.hpp"
#include "server/api/internal/api_util.hpp"

namespace ben_gear::server {

namespace {

bool parse_object_body(const std::string& body, Json& out) {
    if (body.empty()) {
        out = Json::object();
        return true;
    }
    try {
        out = Json::parse(body);
    } catch (...) {
        return false;
    }
    return out.is_object();
}

} // namespace

void register_workbench_routes(Router& router, WorkbenchSnapshotApiService& svc) {
    router.add_route("POST", "/api/workbench/snapshot",
        [svc](const HttpRequest& req) {
            if (!svc.snapshot) return HttpResponse::error(500, "workbench snapshot service unavailable");
            Json request;
            if (!parse_object_body(req.body, request)) return bad_request("invalid JSON object body");
            auto workspace = query_string(req, "workspace");
            if (workspace.empty() && request.contains("workspace") && request["workspace"].is_string()) {
                workspace = container::String(request["workspace"].get<std::string>());
            }
            request.erase("workspace");
            return json_response(svc.snapshot(workspace, req.username, request));
        });

    log::info_fmt("API: workbench routes registered (1)");
}

} // namespace ben_gear::server
