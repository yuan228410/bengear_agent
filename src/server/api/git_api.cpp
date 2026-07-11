#include "server/api/git_api.hpp"

#include "base/log/logger.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>
#include "server/api/internal/api_util.hpp"

namespace ben_gear::server {

namespace {

bool query_bool(const HttpRequest& req, std::string_view key, bool fallback = false) {
    auto value = query_string(req, key);
    if (value.empty()) return fallback;
    auto text = std::string(value.data(), value.size());
    return text == "1" || text == "true" || text == "yes" || text == "on";
}

std::vector<std::string> parse_paths(const Json& body) {
    std::vector<std::string> paths;
    if (!body.contains("paths") || !body["paths"].is_array()) return paths;
    for (const auto& item : body["paths"]) {
        if (!item.is_string()) continue;
        auto path = item.get<std::string>();
        if (!path.empty()) paths.push_back(std::move(path));
    }
    return paths;
}

std::string trim_copy(std::string value) {
    auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), not_space).base(), value.end());
    return value;
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

    router.add_route("GET", "/api/git/worktrees",
        [svc](const HttpRequest& req) {
            if (!svc.worktrees) return HttpResponse::error(500, "git worktrees service unavailable");
            return json_response(svc.worktrees(query_string(req, "workspace"), req.username));
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
            return json_response(svc.switch_branch(workspace, session_id, req.username, name, force));
        });

    router.add_route("POST", "/api/git/branches/delete",
        [svc](const HttpRequest& req) {
            std::string error;
            auto body = parse_body_object(req, error);
            if (!error.empty()) return bad_request(error);
            auto session_id = require_session_id(body, req);
            if (session_id.empty()) return bad_request("missing session_id");
            auto name = std::string(body.value("name", ""));
            if (name.empty()) return bad_request("missing name");
            if (!svc.delete_branch) return HttpResponse::error(500, "git branch delete service unavailable");
            auto force = body.value("force", false);
            auto workspace = workspace_or_default(body, req);
            return json_response(svc.delete_branch(workspace, session_id, req.username, name, force));
        });

    router.add_route("POST", "/api/git/restore",
        [svc](const HttpRequest& req) {
            std::string error;
            auto body = parse_body_object(req, error);
            if (!error.empty()) return bad_request(error);
            auto session_id = require_session_id(body, req);
            if (session_id.empty()) return bad_request("missing session_id");
            auto paths = parse_paths(body);
            if (paths.empty()) return bad_request("paths must be non-empty");
            auto staged = body.value("staged", false);
            auto worktree = body.value("worktree", true);
            if (!staged && !worktree) return bad_request("staged or worktree must be true");
            if (!svc.restore) return HttpResponse::error(500, "git restore service unavailable");
            auto workspace = workspace_or_default(body, req);
            return json_response(svc.restore(workspace, session_id, req.username, paths, staged, worktree));
        });

    router.add_route("POST", "/api/git/commit",
        [svc](const HttpRequest& req) {
            std::string error;
            auto body = parse_body_object(req, error);
            if (!error.empty()) return bad_request(error);
            auto session_id = require_session_id(body, req);
            if (session_id.empty()) return bad_request("missing session_id");
            auto message = trim_copy(std::string(body.value("message", "")));
            if (message.empty()) return bad_request("missing message");
            auto paths = parse_paths(body);
            auto all = body.value("all", false);
            auto amend = body.value("amend", false);
            if (all && !paths.empty()) return bad_request("paths cannot be combined with all");
            if (!svc.commit) return HttpResponse::error(500, "git commit service unavailable");
            auto workspace = workspace_or_default(body, req);
            return json_response(svc.commit(workspace, session_id, req.username, message, paths, all, amend));
        });

    log::info_fmt("API: git routes registered (10)");
}

} // namespace ben_gear::server
