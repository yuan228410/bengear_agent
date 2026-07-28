#include "server/api/handlers.hpp"
#include "log/logger.hpp"

namespace ben_gear::server {

void register_api_routes(Router& router,
                          std::shared_ptr<SessionService> session_svc,
                          std::shared_ptr<ConfigService> config_svc,
                          std::shared_ptr<WorkspaceService> ws_svc,
                          std::shared_ptr<McpService> mcp_svc,
                          std::shared_ptr<FileService> file_svc,
                          std::shared_ptr<workspace::HistoryDB> history_db) {
    register_session_routes(router, session_svc);
    register_config_routes(router, config_svc, ws_svc);
    register_mcp_routes(router, mcp_svc);
    register_file_routes(router, file_svc);
    register_db_routes(router, history_db);
    log::info_fmt("API: all routes registered");
}

} // namespace ben_gear::server
