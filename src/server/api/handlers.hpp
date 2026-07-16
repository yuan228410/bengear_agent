#pragma once

#include "server/core/router.hpp"
#include "server/api/session_api.hpp"
#include "server/api/config_api.hpp"
#include "server/api/mcp_api.hpp"
#include "server/api/file_api.hpp"
#include "server/api/repo_map_api.hpp"
#include "server/api/code_intel_api.hpp"
#include "server/api/workbench_api.hpp"
#include "server/api/workspace_types.hpp"

namespace ben_gear::server {

void register_api_routes(Router& router,
                          SessionService& session_svc,
                          ConfigService& config_svc,
                          WorkspaceService& ws_svc,
                          McpService& mcp_svc,
                          FileService& file_svc,
                          RepoMapApiService& repo_map_svc,
                          CodeIntelApiService& code_intel_svc,
                          WorkbenchSnapshotApiService& workbench_svc);

} // namespace ben_gear::server
