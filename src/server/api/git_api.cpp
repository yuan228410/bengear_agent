#include "ben_gear/server/api/git_api.hpp"

#include "ben_gear/base/log/logger.hpp"

#include <string>

namespace ben_gear::server {

namespace {

container::String query_string(const HttpRequest& req, std::string_view key) {
    auto it = req.query.find(container::String(key));
    if (it == req.query.end()) return container::String();
    return it->second;
}

bool query_bool(const HttpRequest& req, std::string_view key, bool fallback = false) {
    auto value = query_string(req, key);
    if (value.empty()) return fallback;
    auto text = std::string(value.data(), value.size());
    return text == "1" || text == "true" || text == "yes" || text == "on";
}

HttpResponse json_response(const Json& json) {
    return HttpResponse::json(200, json.dump().to_std_string());
}

} // namespace

void register_git_routes(Router& router, GitApiService& svc) {
    router.add_route("GET", "/api/git/status",
        [svc](const HttpRequest& req) {
            if (!svc.status) return HttpResponse::error(500, "git status service unavailable");
            return json_response(svc.status(query_string(req, "workspace"), req.username));
        });

    router.add_route("GET", "/api/git/diff",
        [svc](const HttpRequest& req) {
            if (!svc.diff) return HttpResponse::error(500, "git diff service unavailable");
            return json_response(svc.diff(query_string(req, "workspace"),
                                          req.username,
                                          query_string(req, "path"),
                                          query_bool(req, "staged", false),
                                          query_bool(req, "stat", false),
                                          query_bool(req, "preview", true)));
        });

    log::info_fmt("API: git routes registered (2)");
}

} // namespace ben_gear::server
