#pragma once

#include "ben_gear/application/workspace_resolver.hpp"
#include "ben_gear/config/settings.hpp"
#include "ben_gear/server/api/audit_types.hpp"
#include "ben_gear/server/api/config_types.hpp"
#include "ben_gear/server/api/mcp_types.hpp"
#include "ben_gear/server/api/session_types.hpp"
#include "ben_gear/server/api/workspace_types.hpp"
#include "ben_gear/server/api/file_types.hpp"
#include "ben_gear/server/session/pool.hpp"

namespace ben_gear::server::composition {

struct BasicApiCompositionContext {
    config::Settings& settings;
    application::WorkspaceResolver& workspace_resolver;
    SessionPool& session_pool;
};

SessionService make_session_api_service(BasicApiCompositionContext context);
ConfigService make_config_api_service(BasicApiCompositionContext context);
WorkspaceService make_workspace_api_service(BasicApiCompositionContext context);
McpService make_mcp_api_service();
FileService make_file_api_service();
AuditApiService make_audit_api_service(BasicApiCompositionContext context);

} // namespace ben_gear::server::composition
