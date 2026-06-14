#include "ben_gear/server/api/mcp_api.hpp"
#include "ben_gear/base/log/logger.hpp"

namespace ben_gear::server {

void register_mcp_routes(Router& router, McpService& svc) {
    router.add_route("GET", "/api/mcp/status",
        [&svc](const HttpRequest&) { return HttpResponse::ok(svc.get_status()); });
    log::info_fmt("API: mcp routes registered (1)");
}

} // namespace ben_gear::server
