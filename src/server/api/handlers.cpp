#include "server/api/handlers.hpp"
#include "server/api/session_api.hpp"
#include "server/api/config_api.hpp"
#include "server/api/mcp_api.hpp"
#include "server/api/file_api.hpp"
#include "base/log/logger.hpp"

namespace ben_gear::server {

void register_api_routes(Router& router,
                          std::shared_ptr<SessionService> session_svc,
                          std::shared_ptr<ConfigService> config_svc,
                          std::shared_ptr<WorkspaceService> ws_svc,
                          std::shared_ptr<McpService> mcp_svc,
                          std::shared_ptr<FileService> file_svc) {
    register_session_routes(router, session_svc);
    register_config_routes(router, config_svc, ws_svc);
    register_mcp_routes(router, mcp_svc);
    register_file_routes(router, file_svc);
    log::info_fmt("API: all routes registered");
}

} // namespace ben_gear::server
