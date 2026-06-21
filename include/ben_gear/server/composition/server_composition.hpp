#pragma once

#include "ben_gear/base/container/string.hpp"
#include "ben_gear/config/settings.hpp"
#include "ben_gear/server/api/deps.hpp"
#include "ben_gear/server/api/file_api.hpp"
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
};

struct ServerCompositionContext {
    config::Settings& settings;
    application::WorkspaceResolver& workspace_resolver;
    SessionPool& session_pool;
};

ApiServices make_api_services(ServerCompositionContext context);
RepoMapApiService make_repo_map_api_service(ServerCompositionContext context);
CodeIntelApiService make_code_intel_api_service(ServerCompositionContext context);
void register_composed_api_routes(Router& router, ApiServices& services);

} // namespace ben_gear::server::composition
