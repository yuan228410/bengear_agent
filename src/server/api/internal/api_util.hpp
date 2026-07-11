#pragma once

#include <string>
#include <string_view>

#include "server/core/router.hpp"

// server/api 内部共享请求解析辅助：原在多个 api .cpp 的匿名命名空间重复定义，
// Unity Build 合并编译单元时会冲突，统一改为 inline 单一定义点。

namespace ben_gear::server {

inline container::String query_string(const HttpRequest& req, std::string_view key) {
    auto it = req.query.find(container::String(key));
    if (it == req.query.end()) return container::String();
    return it->second;
}

inline Json parse_body_object(const HttpRequest& req, std::string& error) {
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

inline HttpResponse bad_request(std::string_view message) {
    return HttpResponse::json(400, Json{{"success", false}, {"error_type", "bad_request"}, {"message", std::string(message)}}.dump().to_std_string());
}

inline container::String require_session_id(const Json& body, const HttpRequest& req) {
    auto session_id = body.value("session_id", "");
    if (!session_id.empty()) return container::String(session_id.c_str());
    return query_string(req, "session_id");
}

inline container::String workspace_or_default(const Json& body, const HttpRequest& req) {
    auto workspace = body.value("workspace", "");
    if (!workspace.empty()) return container::String(workspace.c_str());
    return query_string(req, "workspace");
}

inline HttpResponse json_response(const Json& json) {
    auto status = 200;
    if (!json.value("success", true)) {
        auto error_type = std::string(json.value("error_type", ""));
        if (error_type == "permission_not_found" || error_type == "session_not_found") status = 404;
    }
    return HttpResponse::json(status, json.dump().to_std_string());
}

inline container::String param_string(const HttpRequest& req, std::string_view key) {
    auto it = req.params.find(container::String(key));
    if (it == req.params.end()) return container::String();
    return it->second;
}

inline int query_int(const HttpRequest& req, std::string_view key, int fallback = 0) {
    auto value = query_string(req, key);
    if (value.empty()) return fallback;
    try {
        return std::stoi(std::string(value.data(), value.size()));
    } catch (...) {
        return fallback;
    }
}

}  // namespace ben_gear::server
