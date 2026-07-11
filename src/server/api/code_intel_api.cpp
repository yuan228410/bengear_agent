#include "server/api/code_intel_api.hpp"

#include "base/log/logger.hpp"

#include <string>
#include "server/api/internal/api_util.hpp"

namespace ben_gear::server {

namespace {


bool has_valid_position(const container::String& path, int line, int column) {
    return !path.empty() && line > 0 && column > 0;
}

bool has_symbol_or_position(const container::String& symbol, const container::String& path, int line, int column) {
    return !symbol.empty() || has_valid_position(path, line, column);
}

} // namespace

void register_code_intel_routes(Router& router, CodeIntelApiService& svc) {
    router.add_route("GET", "/api/code-intel/capabilities",
        [svc](const HttpRequest& req) {
            if (!svc.capabilities) return HttpResponse::error(500, "code intelligence capabilities service unavailable");
            return json_response(svc.capabilities(query_string(req, "workspace"), req.username));
        });

    router.add_route("GET", "/api/code-intel/document-symbols",
        [svc](const HttpRequest& req) {
            if (!svc.document_symbols) return HttpResponse::error(500, "code intelligence document symbols service unavailable");
            auto path = query_string(req, "path");
            if (path.empty()) return bad_request("missing path");
            return json_response(svc.document_symbols(query_string(req, "workspace"), req.username, path));
        });

    router.add_route("GET", "/api/code-intel/workspace-symbols",
        [svc](const HttpRequest& req) {
            if (!svc.workspace_symbols) return HttpResponse::error(500, "code intelligence workspace symbols service unavailable");
            return json_response(svc.workspace_symbols(query_string(req, "workspace"),
                                                       req.username,
                                                       query_string(req, "query"),
                                                       query_string(req, "kind"),
                                                       query_string(req, "language"),
                                                       query_int(req, "limit", 50)));
        });

    router.add_route("GET", "/api/code-intel/definition",
        [svc](const HttpRequest& req) {
            if (!svc.definition) return HttpResponse::error(500, "code intelligence definition service unavailable");
            auto path = query_string(req, "path");
            auto symbol = query_string(req, "symbol");
            auto line = query_int(req, "line", 0);
            auto column = query_int(req, "column", 0);
            if (!has_symbol_or_position(symbol, path, line, column)) return bad_request("missing symbol or valid path/line/column");
            return json_response(svc.definition(query_string(req, "workspace"), req.username, path, line, column, symbol, query_int(req, "limit", 50)));
        });

    router.add_route("GET", "/api/code-intel/references",
        [svc](const HttpRequest& req) {
            if (!svc.references) return HttpResponse::error(500, "code intelligence references service unavailable");
            auto path = query_string(req, "path");
            auto symbol = query_string(req, "symbol");
            auto line = query_int(req, "line", 0);
            auto column = query_int(req, "column", 0);
            if (!has_symbol_or_position(symbol, path, line, column)) return bad_request("missing symbol or valid path/line/column");
            return json_response(svc.references(query_string(req, "workspace"), req.username, path, line, column, symbol, query_int(req, "limit", 50)));
        });


    router.add_route("POST", "/api/code-intel/context-pack",
        [svc](const HttpRequest& req) {
            if (!svc.context_pack) return HttpResponse::error(500, "code intelligence context pack service unavailable");
            std::string error;
            auto body = parse_body_object(req, error);
            if (!error.empty()) return bad_request(error);
            return json_response(svc.context_pack(query_string(req, "workspace"), req.username, body));
        });

    router.add_route("GET", "/api/code-intel/context-packs/:context_pack_id",
        [svc](const HttpRequest& req) {
            if (!svc.read_context_pack) return HttpResponse::error(500, "code intelligence context pack service unavailable");
            return json_response(svc.read_context_pack(req.username, param_string(req, "context_pack_id")));
        });

    log::info_fmt("API: code intelligence routes registered (7)");
}

} // namespace ben_gear::server
