#include "server/api/audit_api.hpp"

#include "base/log/logger.hpp"

#include <string>

namespace ben_gear::server {

namespace {

container::String query_string(const HttpRequest& req, std::string_view key) {
    auto it = req.query.find(container::String(key));
    if (it == req.query.end()) return container::String();
    return it->second;
}

int query_int(const HttpRequest& req, std::string_view key, int fallback = 0) {
    auto value = query_string(req, key);
    if (value.empty()) return fallback;
    try {
        return std::stoi(std::string(value.data(), value.size()));
    } catch (...) {
        return fallback;
    }
}

HttpResponse json_response(const Json& json) {
    return HttpResponse::json(200, json.dump().to_std_string());
}

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
