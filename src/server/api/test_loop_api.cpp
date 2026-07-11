#include "server/api/test_loop_api.hpp"

#include "base/log/logger.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include "server/api/internal/api_util.hpp"

namespace ben_gear::server {

namespace {

std::string trim_copy(std::string value) {
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

} // namespace

void register_test_loop_routes(Router& router, TestLoopApiService& svc) {
    router.add_route("GET", "/api/test-loop/inspect",
        [svc](const HttpRequest& req) {
            if (!svc.inspect) return HttpResponse::error(500, "test loop inspect service unavailable");
            return json_response(svc.inspect(query_string(req, "workspace"), req.username));
        });

    router.add_route("POST", "/api/test-loop/run",
        [svc](const HttpRequest& req) {
            std::string error;
            auto body = parse_body_object(req, error);
            if (!error.empty()) return bad_request(error);
            auto session_id = require_session_id(body, req);
            if (session_id.empty()) return bad_request("missing session_id");
            auto command = trim_copy(std::string(body.value("command", "")));
            if (command.empty()) return bad_request("missing command");
            auto cwd = std::string(body.value("cwd", "."));
            auto timeout_seconds = body.value("timeout_seconds", 120);
            auto max_output_bytes = body.value("max_output_bytes", 60000);
            if (!svc.run) return HttpResponse::error(500, "test loop run service unavailable");
            auto workspace = workspace_or_default(body, req);
            return json_response(svc.run(workspace, session_id, req.username, command, cwd, timeout_seconds, max_output_bytes));
        });

    log::info_fmt("API: test loop routes registered (2)");
}

} // namespace ben_gear::server
