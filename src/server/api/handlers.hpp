#pragma once

#include "server/core/router.hpp"
#include "server/api/session_api.hpp"
#include "server/api/config_api.hpp"
#include "server/api/mcp_api.hpp"
#include "server/api/file_api.hpp"
#include "server/api/workspace_types.hpp"

#include <memory>

namespace ben_gear::server {

void register_api_routes(Router& router,
                          std::shared_ptr<SessionService> session_svc,
                          std::shared_ptr<ConfigService> config_svc,
                          std::shared_ptr<WorkspaceService> ws_svc,
                          std::shared_ptr<McpService> mcp_svc,
                          std::shared_ptr<FileService> file_svc);

} // namespace ben_gear::server
