#include "ben_gear/server/api/code_intel_api.hpp"

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

    log::info_fmt("API: code intelligence routes registered (5)");
}

} // namespace ben_gear::server
