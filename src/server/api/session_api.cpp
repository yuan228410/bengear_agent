#include "server/api/session_api.hpp"
#include "base/log/logger.hpp"
#include "base/platform/platform.hpp"
#include <algorithm>
#include <ctime>
#include <string>
#include "server/api/internal/api_util.hpp"

namespace ben_gear::server {

namespace {

bool query_bool(const HttpRequest& req, const char* key, bool default_value = false) {
    auto it = req.query.find(key);
    if (it == req.query.end()) return default_value;
    auto value = it->second;
    return value == "1" || value == "true" || value == "yes";
}

std::string export_filename(const std::string& session_id) {
    auto now = std::time(nullptr);
    char buf[32];
    auto tm = ben_gear::base::platform::compat::safe_localtime(now);
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm);
    auto safe_id = session_id.substr(0, std::min<size_t>(session_id.size(), 12));
    return "history_" + safe_id + "_" + buf + ".md";
}

// 验证名称是否安全：拒绝路径穿越字符、控制字符、空字符串
bool is_safe_name(const std::string& name, size_t max_len = 128) {
    if (name.empty() || name.size() > max_len) return false;
    for (char c : name) {
        if (c == '/' || c == '\\' || c == '\0' || c == '\n' || c == '\r' ||
            c == '\t' || static_cast<unsigned char>(c) < 0x20) {
            return false;
        }
    }
    // 拒绝 ".." 和包含 ".." 的路径段
    if (name.find("..") != std::string::npos) return false;
    return true;
}

std::string json_array_response(const std::vector<Json>& items) {
    std::string json;
    json.reserve(2 + items.size() * 128);
    json.push_back('[');
    bool first = true;
    for (const auto& item : items) {
        if (!first) json.push_back(',');
        auto dumped = item.dump();
        json += dumped;
        first = false;
    }
    json.push_back(']');
    return json;
}

}  // namespace

void register_session_routes(Router& router, std::shared_ptr<SessionService> svc) {
    router.add_route("GET", "/api/sessions",
        [svc](const HttpRequest& req) {
            auto sessions = svc->list_sessions(std::string(), req.username);
            return HttpResponse::ok(json_array_response(sessions));
        });

    router.add_route("POST", "/api/sessions",
        [svc](const HttpRequest& req) {
            try {
                // 支持从 body 传 workspace 和 name
                auto ws = std::string();
                auto name = std::string("New Session");
                if (!req.body.empty()) {
                    auto j = Json::parse(req.body);
                    if (j.contains("workspace")) ws = j["workspace"].get<std::string>();
                    if (j.contains("name")) name = j["name"].get<std::string>();
                }
                if (!ws.empty() && !is_safe_name(ws)) return HttpResponse::error(400, "invalid workspace name");
                if (!is_safe_name(name, 256)) return HttpResponse::error(400, "invalid session name");
                auto sid = svc->create_session(name, ws, req.username);
                Json response;
                response["session_id"] = sid;
                return HttpResponse::ok(response.dump());
            } catch (const std::exception&) { return HttpResponse::error(400, "invalid JSON"); }
        });

    router.add_route("DELETE", "/api/sessions/:id",
        [svc](const HttpRequest& req) {
            auto it = req.params.find("id");
            if (it == req.params.end()) return HttpResponse::error(400, "missing id");
            // 从 query 或 body 获取 workspace
            std::string ws;
            auto ws_it = req.query.find("workspace");
            if (ws_it != req.query.end()) ws = ws_it->second;
            return svc->delete_session(it->second, ws, req.username)
                ? HttpResponse::ok("{\"ok\":true}") : HttpResponse::error(404, "not found");
        });

    router.add_route("PUT", "/api/sessions/:id",
        [svc](const HttpRequest& req) {
            auto it = req.params.find("id");
            if (it == req.params.end()) return HttpResponse::error(400, "missing id");
            try {
                auto j = Json::parse(req.body);
                auto name = j.value("name", "");
                if (name.empty()) return HttpResponse::error(400, "missing name");
                std::string ws;
                auto ws_it = req.query.find("workspace");
                if (ws_it != req.query.end()) ws = ws_it->second;
                return svc->rename_session(it->second, name, ws, req.username)
                    ? HttpResponse::ok("{\"ok\":true}") : HttpResponse::error(404, "not found");
            } catch (const std::exception&) { return HttpResponse::error(400, "invalid JSON"); }
        });

    router.add_route("GET", "/api/sessions/:id/history",
        [svc](const HttpRequest& req) {
            auto it = req.params.find("id");
            if (it == req.params.end()) return HttpResponse::error(400, "missing id");
            auto ws_it = req.query.find("workspace");
            auto ws = ws_it != req.query.end() ? ws_it->second : std::string();
            auto msgs = svc->load_history(it->second, ws, query_int(req, "limit", 200), req.username);
            std::string json = "[";
            bool first = true;
            for (const auto& m : msgs) { if (!first) json += ","; json += m.dump(); first = false; }
            json += "]";
            return HttpResponse::ok(json);
        });

    router.add_route("GET", "/api/sessions/:id/export",
        [svc](const HttpRequest& req) {
            auto it = req.params.find("id");
            if (it == req.params.end()) return HttpResponse::error(400, "missing id");
            auto ws_it = req.query.find("workspace");
            auto ws = ws_it != req.query.end() ? ws_it->second : std::string();
            auto session_id = it->second;
            auto content = svc->export_history(
                it->second,
                ws,
                query_bool(req, "include_tool_calls"),
                query_bool(req, "include_thinking"),
                query_bool(req, "include_tool_results"),
                query_int(req, "limit", 0),
                req.username);
            Json response;
            response["filename"] = export_filename(session_id);
            response["content"] = content;
            return HttpResponse::ok(response.dump());
        });

    router.add_route("GET", "/api/workspaces/:name/sessions",
        [svc](const HttpRequest& req) {
            auto it = req.params.find("name");
            if (it == req.params.end()) return HttpResponse::error(400, "missing workspace name");
            auto sessions = svc->list_sessions_by_workspace(it->second, req.username);
            return HttpResponse::ok(json_array_response(sessions));
        });

    log::info_fmt("API: session routes registered (6)");
}

} // namespace ben_gear::server
