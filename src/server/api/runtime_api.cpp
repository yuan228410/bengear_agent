#include "server/api/runtime_api.hpp"

#include "base/log/logger.hpp"

#include <string>
#include "server/api/internal/api_util.hpp"

namespace ben_gear::server {

namespace {


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


    router.add_route("GET", "/api/runtime/workflows/:workflow_id/timeline",
        [svc](const HttpRequest& req) {
            if (!svc.workflow_timeline) return HttpResponse::error(500, "runtime workflow timeline service unavailable");
            return json_response(svc.workflow_timeline(req.username, param_string(req, "workflow_id")));
        });

    router.add_route("GET", "/api/runtime/workflows/:workflow_id/integrity",
        [svc](const HttpRequest& req) {
            if (!svc.workflow_integrity) return HttpResponse::error(500, "runtime workflow integrity service unavailable");
            return json_response(svc.workflow_integrity(req.username, param_string(req, "workflow_id")));
        });

    router.add_route("POST", "/api/runtime/workflows/compact",
        [svc](const HttpRequest& req) {
            if (!svc.compact_workflows) return HttpResponse::error(500, "runtime workflow compact service unavailable");
            return json_response(svc.compact_workflows(req.username));
        });

    log::info_fmt("API: runtime routes registered (13)");
}

} // namespace ben_gear::server
