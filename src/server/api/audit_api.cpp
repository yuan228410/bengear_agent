#include "server/api/audit_api.hpp"

#include "base/log/logger.hpp"

#include <string>
#include "server/api/internal/api_util.hpp"

namespace ben_gear::server {

namespace {

} // namespace

void register_audit_routes(Router& router, AuditApiService& svc) {
    router.add_route("GET", "/api/audit/events",
        [svc](const HttpRequest& req) {
            if (!svc.list_events) return HttpResponse::error(500, "audit events service unavailable");
            return json_response(svc.list_events(query_string(req, "workspace"),
                                                 query_string(req, "session_id"),
                                                 req.username,
                                                 query_string(req, "category"),
                                                 query_string(req, "action"),
                                                 query_int(req, "limit", 100)));
        });

    log::info_fmt("API: audit routes registered (1)");
}

} // namespace ben_gear::server
