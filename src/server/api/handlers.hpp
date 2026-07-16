#pragma once

#include "server/core/router.hpp"
#include "server/api/session_api.hpp"
#include "server/api/config_api.hpp"
#include "server/api/mcp_api.hpp"
#include "server/api/file_api.hpp"
#include "server/api/workspace_types.hpp"

namespace ben_gear::server {

void register_api_routes(Router& router,
                          SessionService& session_svc,
                          ConfigService& config_svc,
                          WorkspaceService& ws_svc,
                          McpService& mcp_svc,
                          FileService& file_svc);

} // namespace ben_gear::server
