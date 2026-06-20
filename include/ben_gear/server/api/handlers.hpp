#pragma once

#include "ben_gear/server/core/router.hpp"
#include "ben_gear/server/api/deps.hpp"
#include "ben_gear/server/api/file_api.hpp"
#include "ben_gear/server/api/git_api.hpp"
#include "ben_gear/server/api/permission_api.hpp"
#include "ben_gear/server/api/patch_api.hpp"
#include "ben_gear/server/api/checkpoint_api.hpp"
#include "ben_gear/server/api/test_loop_api.hpp"
#include "ben_gear/server/api/repo_map_api.hpp"
#include "ben_gear/server/api/code_intel_api.hpp"
#include "ben_gear/server/api/audit_api.hpp"

namespace ben_gear::server {

void register_api_routes(Router& router,
                          SessionService& session_svc,
                          ConfigService& config_svc,
                          WorkspaceService& ws_svc,
                          McpService& mcp_svc,
                          FileService& file_svc,
                          GitApiService& git_svc,
                          PermissionApiService& permission_svc,
                          PatchApiService& patch_svc,
                          CheckpointApiService& checkpoint_svc,
                          TestLoopApiService& test_loop_svc,
                          RepoMapApiService& repo_map_svc,
                          CodeIntelApiService& code_intel_svc,
                          AuditApiService& audit_svc);

} // namespace ben_gear::server
