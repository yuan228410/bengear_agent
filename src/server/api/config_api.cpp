#include "ben_gear/server/api/config_api.hpp"
#include "ben_gear/base/log/logger.hpp"
#include <string>

namespace ben_gear::server {

void register_config_routes(Router& router, ConfigService& cfg, WorkspaceService& ws) {
    router.add_route("GET", "/api/config",
        [cfg](const HttpRequest&) {
            auto info = cfg.get_config();
            return HttpResponse::ok("{\"model\":\"" + std::string(info.model.c_str())
                + "\",\"provider\":\"" + std::string(info.provider.c_str())
                + "\",\"workspace\":\"" + std::string(info.workspace.c_str())
                + "\",\"version\":\"" + std::string(info.version.c_str()) + "\"}");
        });

    router.add_route("GET", "/api/models",
        [cfg](const HttpRequest&) {
            auto info = cfg.get_config();
            auto display = info.display_name.empty() ? info.model : info.display_name;
            return HttpResponse::ok("{\"models\":[{\"id\":\"" + std::string(info.model.c_str())
                + "\",\"name\":\"" + std::string(display.c_str()) + "\",\"current\":true}]}");
        });

    router.add_route("POST", "/api/models/switch",
        [cfg](const HttpRequest& req) {
            try {
                auto j = Json::parse(req.body);
                auto model = j.value("model", "");
                if (model.empty()) return HttpResponse::error(400, "missing model");
                cfg.set_model(container::String(model.c_str()));
                return HttpResponse::ok("{\"ok\":true}");
            } catch (const std::exception& e) { return HttpResponse::error(500, e.what()); }
        });

    router.add_route("GET", "/api/workspaces",
        [ws](const HttpRequest& req) {
            auto workspaces = ws.list_workspaces(req.username);
            std::string json = "{\"workspaces\":[";
            bool first = true;
            for (const auto& w : workspaces) {
                if (!first) json += ",";
                json += "{\"name\":\"" + std::string(w.name.c_str()) + "\",\"path\":\"" + w.path + "\"}";
                first = false;
            }
            json += "]}";
            return HttpResponse::ok(json);
        });

    router.add_route("POST", "/api/workspaces",
        [ws](const HttpRequest& req) {
            try {
                auto j = Json::parse(req.body);
                auto name = j.value("name", "");
                auto project_path = j.value("project_path", "");

                // 支持传递 path 自动提取名称
                auto path_val = j.value("path", "");
                if (name.empty() && !path_val.empty()) {
                    std::string p = path_val;
                    // 移除末尾斜杠
                    while (!p.empty() && p.back() == '/') p.pop_back();
                    auto pos = p.find_last_of('/');
                    name = (pos == std::string::npos) ? p : p.substr(pos + 1);
                    if (project_path.empty()) project_path = path_val;
                }

                if (name.empty()) return HttpResponse::error(400, "missing name");
                auto result = ws.create_workspace(
                    container::String(name.c_str()),
                    container::String(project_path.c_str()),
                    req.username);
                if (!result) return HttpResponse::error(409, "workspace already exists");
                return HttpResponse::ok("{\"name\":\"" + std::string(result->name.c_str())
                    + "\",\"path\":\"" + result->path + "\"}");
            } catch (const std::exception& e) {
                return HttpResponse::error(500, e.what());
            }
        });

    router.add_route("DELETE", "/api/workspaces/:name",
        [ws](const HttpRequest& req) {
            auto it = req.params.find("name");
            if (it == req.params.end()) return HttpResponse::error(400, "missing name");
            return ws.delete_workspace(it->second, req.username)
                ? HttpResponse::ok("{\"ok\":true}")
                : HttpResponse::error(404, "workspace not found");
        });

    log::info_fmt("API: config routes registered (6)");
}

} // namespace ben_gear::server
