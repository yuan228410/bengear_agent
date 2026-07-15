#include "server/api/checkpoint_api.hpp"

#include "base/log/logger.hpp"

#include <string>
#include <vector>
#include "server/api/internal/api_util.hpp"

namespace ben_gear::server {

namespace {

std::string path_param(const HttpRequest& req, std::string_view key) {
    auto it = req.params.find(std::string(key));
    if (it == req.params.end()) return {};
    return std::string(it->second.data(), it->second.size());
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

Json strip_checkpoint_content(Json result) {
    if (!result.value("success", false) || !result.contains("checkpoint") || !result["checkpoint"].is_object()) return result;
    Json checkpoint = result["checkpoint"];
    if (checkpoint.contains("files") && checkpoint["files"].is_array()) {
        Json files = Json::array();
        for (const auto& item : checkpoint["files"]) {
            Json file = item;
            file.erase("content");
            files.push_back(std::move(file));
        }
        checkpoint["files"] = std::move(files);
        result["checkpoint"] = std::move(checkpoint);
    }
    return result;
}

} // namespace

void register_checkpoint_routes(Router& router, CheckpointApiService& svc) {
    router.add_route("GET", "/api/checkpoints",
        [svc](const HttpRequest& req) {
            auto session_id = query_string(req, "session_id");
            if (session_id.empty()) return bad_request("missing session_id");
            if (!svc.list) return HttpResponse::error(500, "checkpoint list service unavailable");
            return json_response(svc.list(query_string(req, "workspace"), session_id, req.username));
        });

    router.add_route("GET", "/api/checkpoints/:checkpoint_id",
        [svc](const HttpRequest& req) {
            auto session_id = query_string(req, "session_id");
            if (session_id.empty()) return bad_request("missing session_id");
            auto checkpoint_id = path_param(req, "checkpoint_id");
            if (checkpoint_id.empty()) return bad_request("missing checkpoint_id");
            if (!svc.read) return HttpResponse::error(500, "checkpoint read service unavailable");
            return json_response(strip_checkpoint_content(svc.read(query_string(req, "workspace"), session_id, req.username, checkpoint_id)));
        });

    router.add_route("POST", "/api/checkpoints/:checkpoint_id/restore",
        [svc](const HttpRequest& req) {
            std::string error;
            auto body = parse_body_object(req, error);
            if (!error.empty()) return bad_request(error);
            auto session_id = require_session_id(body, req);
            if (session_id.empty()) return bad_request("missing session_id");
            auto checkpoint_id = path_param(req, "checkpoint_id");
            if (checkpoint_id.empty()) return bad_request("missing checkpoint_id");
            if (!svc.restore) return HttpResponse::error(500, "checkpoint restore service unavailable");
            auto paths = parse_paths(body);
            auto force = body.value("force", false);
            auto workspace = workspace_or_default(body, req);
            return json_response(svc.restore(workspace, session_id, req.username, checkpoint_id, paths, force));
        });

    router.add_route("DELETE", "/api/checkpoints/:checkpoint_id",
        [svc](const HttpRequest& req) {
            std::string error;
            auto body = parse_body_object(req, error);
            if (!error.empty()) return bad_request(error);
            auto session_id = require_session_id(body, req);
            if (session_id.empty()) return bad_request("missing session_id");
            auto checkpoint_id = path_param(req, "checkpoint_id");
            if (checkpoint_id.empty()) return bad_request("missing checkpoint_id");
            if (!svc.remove) return HttpResponse::error(500, "checkpoint delete service unavailable");
            auto workspace = workspace_or_default(body, req);
            return json_response(svc.remove(workspace, session_id, req.username, checkpoint_id));
        });

    log::info_fmt("API: checkpoint routes registered (4)");
}

} // namespace ben_gear::server
