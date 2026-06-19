#include "ben_gear/server/api/git_api.hpp"

#include "ben_gear/base/log/logger.hpp"

#include <optional>
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

HttpResponse bad_request(std::string_view message) {
    return HttpResponse::json(400, Json{{"success", false}, {"error_type", "bad_request"}, {"message", std::string(message)}}.dump().to_std_string());
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

std::optional<HttpResponse> check_permission(const GitApiService& svc,
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

    router.add_route("POST", "/api/git/branches",
        [svc](const HttpRequest& req) {
            std::string error;
            auto body = parse_body_object(req, error);
            if (!error.empty()) return bad_request(error);
            auto session_id = require_session_id(body, req);
            if (session_id.empty()) return bad_request("missing session_id");
            auto name = std::string(body.value("name", ""));
            if (name.empty()) return bad_request("missing name");
            if (!svc.create_branch) return HttpResponse::error(500, "git branch create service unavailable");
            auto start_point = std::string(body.value("start_point", ""));
            auto force = body.value("force", false);
            auto workspace = workspace_or_default(body, req);
            Json arguments{{"action", "create"}, {"name", name}, {"start_point", start_point}, {"force", force}};
            if (auto blocked = check_permission(svc, workspace, session_id, req.username, "git_branch", arguments)) return *blocked;
            return json_response(svc.create_branch(workspace, session_id, req.username, name, start_point, force));
        });

    router.add_route("POST", "/api/git/branches/switch",
        [svc](const HttpRequest& req) {
            std::string error;
            auto body = parse_body_object(req, error);
            if (!error.empty()) return bad_request(error);
            auto session_id = require_session_id(body, req);
            if (session_id.empty()) return bad_request("missing session_id");
            auto name = std::string(body.value("name", ""));
            if (name.empty()) return bad_request("missing name");
            if (!svc.switch_branch) return HttpResponse::error(500, "git branch switch service unavailable");
            auto force = body.value("force", false);
            auto workspace = workspace_or_default(body, req);
            Json arguments{{"action", "switch"}, {"name", name}, {"force", force}};
            if (auto blocked = check_permission(svc, workspace, session_id, req.username, "git_branch", arguments)) return *blocked;
            return json_response(svc.switch_branch(workspace, session_id, req.username, name, force));
        });

    log::info_fmt("API: git routes registered (6)");
}

} // namespace ben_gear::server
