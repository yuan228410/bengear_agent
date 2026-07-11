#pragma once

#include "base/container/string.hpp"
#include "base/config/settings.hpp"
#include "server/api/audit_types.hpp"
#include "server/api/runtime_types.hpp"
#include "server/api/checkpoint_types.hpp"
#include "server/api/code_intel_types.hpp"
#include "server/api/config_types.hpp"
#include "server/api/diagnostic_types.hpp"
#include "server/api/git_types.hpp"
#include "server/api/mcp_types.hpp"
#include "server/api/patch_types.hpp"
#include "server/api/permission_types.hpp"
#include "server/api/repo_map_types.hpp"
#include "server/api/session_types.hpp"
#include "server/api/test_loop_types.hpp"
#include "server/api/workspace_types.hpp"
#include "server/api/workbench_types.hpp"
#include "server/api/file_types.hpp"
#include "server/core/router.hpp"
#include "server/session/pool.hpp"
#include "workspace/history_db.hpp"
#include "workspace/manager.hpp"
#include "application/workspace_resolver.hpp"

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
    RuntimeApiService runtime;
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
RuntimeApiService make_runtime_api_service(ServerCompositionContext context);
WorkbenchSnapshotApiService make_workbench_snapshot_api_service(ServerCompositionContext context);
void register_composed_api_routes(Router& router, ApiServices& services);

} // namespace ben_gear::server::composition
