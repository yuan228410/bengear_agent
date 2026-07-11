#include "server/api/repo_map_api.hpp"

#include "base/log/logger.hpp"

#include <string>
#include "server/api/internal/api_util.hpp"

namespace ben_gear::server {

namespace {

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
