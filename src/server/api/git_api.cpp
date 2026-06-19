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

    router.add_route("GET", "/api/git/log",
        [svc](const HttpRequest& req) {
            if (!svc.log) return HttpResponse::error(500, "git log service unavailable");
            return json_response(svc.log(query_string(req, "workspace"),
                                         req.username,
                                         query_string(req, "path"),
                                         query_int(req, "limit", 20)));
        });

    router.add_route("GET", "/api/git/branches",
        [svc](const HttpRequest& req) {
            if (!svc.branches) return HttpResponse::error(500, "git branches service unavailable");
            return json_response(svc.branches(query_string(req, "workspace"), req.username));
        });

    log::info_fmt("API: git routes registered (4)");
}

} // namespace ben_gear::server
