#include "ben_gear/server/api/repo_map_api.hpp"

#include "ben_gear/base/log/logger.hpp"

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

HttpResponse bad_request(std::string_view message) {
    return HttpResponse::json(400, Json{{"success", false}, {"error_type", "bad_request"}, {"message", std::string(message)}}.dump().to_std_string());
}

} // namespace

void register_repo_map_routes(Router& router, RepoMapApiService& svc) {
    router.add_route("GET", "/api/repo-map/overview",
        [svc](const HttpRequest& req) {
            if (!svc.overview) return HttpResponse::error(500, "repo map overview service unavailable");
            return json_response(svc.overview(query_string(req, "workspace"), req.username));
        });

    router.add_route("GET", "/api/repo-map/files",
        [svc](const HttpRequest& req) {
            if (!svc.find_files) return HttpResponse::error(500, "repo map files service unavailable");
            return json_response(svc.find_files(query_string(req, "workspace"),
                                                req.username,
                                                query_string(req, "query"),
                                                query_string(req, "kind"),
                                                query_string(req, "language"),
                                                query_int(req, "limit", 50)));
        });

    router.add_route("GET", "/api/repo-map/symbols",
        [svc](const HttpRequest& req) {
            if (!svc.find_symbols) return HttpResponse::error(500, "repo map symbols service unavailable");
            return json_response(svc.find_symbols(query_string(req, "workspace"),
                                                  req.username,
                                                  query_string(req, "query"),
                                                  query_string(req, "kind"),
                                                  query_string(req, "language"),
                                                  query_int(req, "limit", 50)));
        });

    router.add_route("GET", "/api/repo-map/explain",
        [svc](const HttpRequest& req) {
            if (!svc.explain_path) return HttpResponse::error(500, "repo map explain service unavailable");
            auto path = query_string(req, "path");
            if (path.empty()) return bad_request("missing path");
            return json_response(svc.explain_path(query_string(req, "workspace"), req.username, path));
        });

    log::info_fmt("API: repo map routes registered (4)");
}

} // namespace ben_gear::server
