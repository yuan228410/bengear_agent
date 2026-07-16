#include "server/api/handlers.hpp"
#include "server/api/session_api.hpp"
#include "server/api/config_api.hpp"
#include "server/api/mcp_api.hpp"
#include "server/api/file_api.hpp"
#include "server/api/repo_map_api.hpp"
#include "server/api/code_intel_api.hpp"
#include "server/api/workbench_api.hpp"
#include "base/log/logger.hpp"

namespace ben_gear::server {

void register_api_routes(Router& router,
                          SessionService& session_svc,
                          ConfigService& config_svc,
                          WorkspaceService& ws_svc,
                          McpService& mcp_svc,
                          FileService& file_svc,
                          RepoMapApiService& repo_map_svc,
                          CodeIntelApiService& code_intel_svc,
                          WorkbenchSnapshotApiService& workbench_svc) {
    register_session_routes(router, session_svc);
    register_config_routes(router, config_svc, ws_svc);
    register_mcp_routes(router, mcp_svc);
    register_file_routes(router, file_svc);
    register_repo_map_routes(router, repo_map_svc);
    register_code_intel_routes(router, code_intel_svc);
    register_workbench_routes(router, workbench_svc);
    log::info_fmt("API: all routes registered");
}

} // namespace ben_gear::server
