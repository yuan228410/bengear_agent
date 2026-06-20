#include "ben_gear/server/api/test_loop_api.hpp"

#include "ben_gear/base/log/logger.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>

namespace ben_gear::server {

namespace {

container::String query_string(const HttpRequest& req, std::string_view key) {
    auto it = req.query.find(container::String(key));
    if (it == req.query.end()) return container::String();
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

std::string trim_copy(std::string value) {
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
}

HttpResponse json_response(const Json& json) {
    return HttpResponse::json(200, json.dump().to_std_string());
}

HttpResponse bad_request(std::string_view message) {
    return HttpResponse::json(400, Json{{"success", false}, {"error_type", "bad_request"}, {"message", std::string(message)}}.dump().to_std_string());
}

container::String require_session_id(const Json& body, const HttpRequest& req) {
    auto session_id = body.value("session_id", "");
    if (!session_id.empty()) return container::String(session_id.c_str());
    return query_string(req, "session_id");
}

container::String workspace_or_default(const Json& body, const HttpRequest& req) {
    auto workspace = body.value("workspace", "");
    if (!workspace.empty()) return container::String(workspace.c_str());
    return query_string(req, "workspace");
}

bool permission_allows(const Json& decision) {
    if (!decision.is_object()) return false;
    if (decision.value("success", false)) return true;
    auto effect = std::string(decision.value("policy_effect", ""));
    return effect == "allow";
}

std::optional<HttpResponse> check_permission(const TestLoopApiService& svc,
                                             const container::String& workspace,
                                             const container::String& session_id,
                                             const container::String& username,
                                             std::string_view tool_name,
                                             const Json& arguments) {
    if (!svc.check_permission) return std::nullopt;
    auto decision = svc.check_permission(workspace, session_id, username, tool_name, arguments);
    if (permission_allows(decision)) return std::nullopt;
    return json_response(decision);
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
            Json arguments{{"command", command}, {"cwd", cwd}, {"timeout_seconds", timeout_seconds}, {"max_output_bytes", max_output_bytes}};
            if (auto blocked = check_permission(svc, workspace, session_id, req.username, "run_tests", arguments)) return *blocked;
            return json_response(svc.run(workspace, session_id, req.username, command, cwd, timeout_seconds, max_output_bytes));
        });

    log::info_fmt("API: test loop routes registered (2)");
}

} // namespace ben_gear::server
