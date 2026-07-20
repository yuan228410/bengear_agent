#include "server/api/file_api.hpp"
#include "log/logger.hpp"

namespace ben_gear::server {

void register_file_routes(Router& router, std::shared_ptr<FileService> svc) {
    router.add_route("GET", "/api/files/home",
        [svc](const HttpRequest& req) {
            auto home = svc->home_directory(req.username);
            Json response;
            response["path"] = home;
            return HttpResponse::ok(response.dump());
        });

    router.add_route("GET", "/api/files/list",
        [svc](const HttpRequest& req) {
            std::string query_path;
            auto it = req.query.find("path");
            if (it != req.query.end()) {
                query_path = it->second;
            }

            auto entries = svc->list_files(query_path, req.username);

            Json response = Json::array();
            for (const auto& e : entries) {
                Json item;
                item["name"] = e.name;
                item["type"] = e.type;
                item["size"] = static_cast<int64_t>(e.size);
                item["modified"] = e.modified;
                response.push_back(std::move(item));
            }
            return HttpResponse::ok(response.dump());
        });

    log::info_fmt("API: file routes registered (2)");
}

} // namespace ben_gear::server
