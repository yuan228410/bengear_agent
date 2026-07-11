#pragma once

#include "server/core/router.hpp"
#include "server/api/session_api.hpp"
#include "server/api/config_api.hpp"
#include "server/api/mcp_api.hpp"
#include "server/api/file_api.hpp"
#include "server/api/git_api.hpp"
#include "server/api/permission_api.hpp"
#include "server/api/patch_api.hpp"
#include "server/api/checkpoint_api.hpp"
#include "server/api/test_loop_api.hpp"
#include "server/api/diagnostic_context_api.hpp"
#include "server/api/diagnostic_repair_api.hpp"
#include "server/api/repo_map_api.hpp"
#include "server/api/code_intel_api.hpp"
#include "server/api/audit_api.hpp"
#include "server/api/runtime_api.hpp"
#include "server/api/workbench_api.hpp"

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
                          DiagnosticContextApiService& diagnostic_context_svc,
                          DiagnosticRepairApiService& diagnostic_repair_svc,
                          RepoMapApiService& repo_map_svc,
                          CodeIntelApiService& code_intel_svc,
                          AuditApiService& audit_svc,
                          RuntimeApiService& runtime_svc,
                          WorkbenchSnapshotApiService& workbench_svc);

} // namespace ben_gear::server
