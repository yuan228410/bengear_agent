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


Json parse_body_object(const HttpRequest& req, std::string& error) {
    if (req.body.empty()) return Json::object();
    try {
        auto json = Json::parse(req.body);
        if (!json.is_object()) {
            error = "request body must be a JSON object";
            return Json();
        }
        return json;
    } catch (const std::exception& e) {
        error = e.what();
        return Json();
    }
}

HttpResponse bad_request(std::string_view message) {
    return HttpResponse::json(400, Json{{"success", false}, {"error_type", "bad_request"}, {"message", std::string(message)}}.dump().to_std_string());
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


    router.add_route("GET", "/api/runtime/executions/:execution_id/links",
        [svc](const HttpRequest& req) {
            if (!svc.list_links) return HttpResponse::error(500, "runtime link service unavailable");
            return json_response(svc.list_links(query_string(req, "workspace"),
                                                query_string(req, "session_id"),
                                                req.username,
                                                param_string(req, "execution_id"),
                                                query_string(req, "relation"),
                                                query_int(req, "limit", 100)));
        });

    router.add_route("POST", "/api/runtime/executions/:execution_id/links",
        [svc](const HttpRequest& req) {
            if (!svc.append_link) return HttpResponse::error(500, "runtime link service unavailable");
            std::string error;
            auto body = parse_body_object(req, error);
            if (!error.empty()) return bad_request(error);
            return json_response(svc.append_link(query_string(req, "workspace"),
                                                 query_string(req, "session_id"),
                                                 req.username,
                                                 param_string(req, "execution_id"),
                                                 body));
        });


    router.add_route("GET", "/api/runtime/workflows",
        [svc](const HttpRequest& req) {
            if (!svc.list_workflows) return HttpResponse::error(500, "runtime workflow service unavailable");
            return json_response(svc.list_workflows(query_string(req, "workspace"),
                                                    query_string(req, "session_id"),
                                                    req.username,
                                                    query_string(req, "status"),
                                                    query_string(req, "source_execution_id"),
                                                    query_int(req, "limit", 100)));
        });

    router.add_route("GET", "/api/runtime/workflows/:workflow_id",
        [svc](const HttpRequest& req) {
            if (!svc.read_workflow) return HttpResponse::error(500, "runtime workflow service unavailable");
            return json_response(svc.read_workflow(req.username, param_string(req, "workflow_id")));
        });

    router.add_route("POST", "/api/runtime/workflows/repair",
        [svc](const HttpRequest& req) {
            if (!svc.start_repair_workflow) return HttpResponse::error(500, "runtime workflow service unavailable");
            std::string error;
            auto body = parse_body_object(req, error);
            if (!error.empty()) return bad_request(error);
            return json_response(svc.start_repair_workflow(query_string(req, "workspace"),
                                                           query_string(req, "session_id"),
                                                           req.username,
                                                           body));
        });

    router.add_route("POST", "/api/runtime/workflows/:workflow_id/resume",
        [svc](const HttpRequest& req) {
            if (!svc.resume_workflow) return HttpResponse::error(500, "runtime workflow service unavailable");
            std::string error;
            auto body = parse_body_object(req, error);
            if (!error.empty()) return bad_request(error);
            return json_response(svc.resume_workflow(req.username, param_string(req, "workflow_id"), body));
        });

    router.add_route("POST", "/api/runtime/workflows/:workflow_id/cancel",
        [svc](const HttpRequest& req) {
            if (!svc.cancel_workflow) return HttpResponse::error(500, "runtime workflow service unavailable");
            return json_response(svc.cancel_workflow(req.username, param_string(req, "workflow_id")));
        });

    log::info_fmt("API: runtime routes registered (10)");
}

} // namespace ben_gear::server
