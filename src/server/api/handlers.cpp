#include "ben_gear/server/api/handlers.hpp"
#include "ben_gear/server/api/session_api.hpp"
#include "ben_gear/server/api/config_api.hpp"
#include "ben_gear/server/api/mcp_api.hpp"
#include "ben_gear/server/api/file_api.hpp"
#include "ben_gear/server/api/git_api.hpp"
#include "ben_gear/server/api/permission_api.hpp"
#include "ben_gear/server/api/patch_api.hpp"
#include "ben_gear/server/api/checkpoint_api.hpp"
#include "ben_gear/server/api/test_loop_api.hpp"
#include "ben_gear/server/api/diagnostic_context_api.hpp"
#include "ben_gear/server/api/diagnostic_repair_api.hpp"
#include "ben_gear/server/api/repo_map_api.hpp"
#include "ben_gear/server/api/code_intel_api.hpp"
#include "ben_gear/server/api/audit_api.hpp"
#include "ben_gear/server/api/runtime_api.hpp"
#include "ben_gear/server/api/workbench_api.hpp"
#include "ben_gear/base/log/logger.hpp"

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
                          WorkbenchSnapshotApiService& workbench_svc) {
    register_session_routes(router, session_svc);
    register_config_routes(router, config_svc, ws_svc);
    register_mcp_routes(router, mcp_svc);
    register_file_routes(router, file_svc);
    register_git_routes(router, git_svc);
    register_permission_routes(router, permission_svc);
    register_patch_routes(router, patch_svc);
    register_checkpoint_routes(router, checkpoint_svc);
    register_test_loop_routes(router, test_loop_svc);
    register_diagnostic_context_routes(router, diagnostic_context_svc);
    register_diagnostic_repair_routes(router, diagnostic_repair_svc);
    register_repo_map_routes(router, repo_map_svc);
    register_code_intel_routes(router, code_intel_svc);
    register_audit_routes(router, audit_svc);
    register_runtime_routes(router, runtime_svc);
    register_workbench_routes(router, workbench_svc);
    log::info_fmt("API: all routes registered");
}

} // namespace ben_gear::server
