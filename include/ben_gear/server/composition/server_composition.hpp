#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/config/settings.hpp"
#include "ben_gear/server/api/audit_types.hpp"
#include "ben_gear/server/api/checkpoint_types.hpp"
#include "ben_gear/server/api/code_intel_types.hpp"
#include "ben_gear/server/api/config_types.hpp"
#include "ben_gear/server/api/diagnostic_types.hpp"
#include "ben_gear/server/api/git_types.hpp"
#include "ben_gear/server/api/mcp_types.hpp"
#include "ben_gear/server/api/patch_types.hpp"
#include "ben_gear/server/api/permission_types.hpp"
#include "ben_gear/server/api/repo_map_types.hpp"
#include "ben_gear/server/api/session_types.hpp"
#include "ben_gear/server/api/test_loop_types.hpp"
#include "ben_gear/server/api/workspace_types.hpp"
#include "ben_gear/server/api/workbench_types.hpp"
#include "ben_gear/server/api/file_types.hpp"
#include "ben_gear/server/core/router.hpp"
#include "ben_gear/server/session/pool.hpp"
#include "ben_gear/workspace/history_db.hpp"
#include "ben_gear/workspace/manager.hpp"
#include "ben_gear/application/workspace_resolver.hpp"

namespace ben_gear::server::composition {

struct ApiServices {
    SessionService session;
    ConfigService config;
    WorkspaceService workspace;
    McpService mcp;
    FileService file;
    GitApiService git;
    PermissionApiService permission;
    PatchApiService patch;
    CheckpointApiService checkpoint;
    TestLoopApiService test_loop;
    DiagnosticContextApiService diagnostic_context;
    DiagnosticRepairApiService diagnostic_repair;
    RepoMapApiService repo_map;
    CodeIntelApiService code_intel;
    AuditApiService audit;
    WorkbenchSnapshotApiService workbench;
};

struct ServerCompositionContext {
    config::Settings& settings;
    application::WorkspaceResolver& workspace_resolver;
    SessionPool& session_pool;
};

ApiServices make_api_services(ServerCompositionContext context);
DiagnosticContextApiService make_diagnostic_context_api_service(ServerCompositionContext context);
DiagnosticRepairApiService make_diagnostic_repair_api_service(ServerCompositionContext context);
RepoMapApiService make_repo_map_api_service(ServerCompositionContext context);
CodeIntelApiService make_code_intel_api_service(ServerCompositionContext context);
WorkbenchSnapshotApiService make_workbench_snapshot_api_service(ServerCompositionContext context);
void register_composed_api_routes(Router& router, ApiServices& services);

} // namespace ben_gear::server::composition
