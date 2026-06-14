#include "ben_gear/server/api/handlers.hpp"
#include "ben_gear/server/api/session_api.hpp"
#include "ben_gear/server/api/config_api.hpp"
#include "ben_gear/server/api/mcp_api.hpp"
#include "ben_gear/server/api/file_api.hpp"
#include "ben_gear/base/log/logger.hpp"

namespace ben_gear::server {

void register_api_routes(Router& router,
                          SessionService& session_svc,
                          ConfigService& config_svc,
                          WorkspaceService& ws_svc,
                          McpService& mcp_svc,
                          FileService& file_svc) {
    register_session_routes(router, session_svc);
    register_config_routes(router, config_svc, ws_svc);
    register_mcp_routes(router, mcp_svc);
    register_file_routes(router, file_svc);
    log::info_fmt("API: all routes registered");
}

} // namespace ben_gear::server
