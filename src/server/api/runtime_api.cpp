#include "ben_gear/server/api/runtime_api.hpp"

#include "ben_gear/base/log/logger.hpp"

#include <string>

namespace ben_gear::server {

namespace {

container::String query_string(const HttpRequest& req, std::string_view key) {
    auto it = req.query.find(container::String(key));
    if (it == req.query.end()) return container::String();
    return it->second;
}

container::String param_string(const HttpRequest& req, std::string_view key) {
    auto it = req.params.find(container::String(key));
    if (it == req.params.end()) return container::String();
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
    auto success = json.value("success", true);
    auto status = success ? 200 : (json.value("error_type", "") == std::string("execution_not_found") ? 404 : 500);
    return HttpResponse::json(status, json.dump().to_std_string());
}

} // namespace

void register_runtime_routes(Router& router, RuntimeApiService& svc) {
    router.add_route("GET", "/api/runtime/executions",
        [svc](const HttpRequest& req) {
            if (!svc.list_executions) return HttpResponse::error(500, "runtime execution service unavailable");
            return json_response(svc.list_executions(query_string(req, "workspace"),
                                                     query_string(req, "session_id"),
                                                     req.username,
                                                     query_string(req, "action"),
                                                     query_string(req, "status"),
                                                     query_string(req, "capability"),
                                                     query_int(req, "limit", 100)));
        });

    router.add_route("GET", "/api/runtime/executions/:execution_id",
        [svc](const HttpRequest& req) {
            if (!svc.read_execution) return HttpResponse::error(500, "runtime execution service unavailable");
            return json_response(svc.read_execution(req.username, param_string(req, "execution_id")));
        });

    router.add_route("GET", "/api/runtime/executions/:execution_id/trace",
        [svc](const HttpRequest& req) {
            if (!svc.read_execution) return HttpResponse::error(500, "runtime execution service unavailable");
            auto result = svc.read_execution(req.username, param_string(req, "execution_id"));
            if (!result.value("success", false)) return json_response(result);
            auto execution = result.value("execution", Json::object());
            return json_response(Json{{"success", true},
                                      {"execution_id", execution.value("execution_id", "")},
                                      {"trace", execution.contains("execution") ? execution["execution"].value("trace", Json::array()) : Json::array()}});
        });

    log::info_fmt("API: runtime routes registered (3)");
}

} // namespace ben_gear::server
