#pragma once

#include <memory>

#include "base/config/settings.hpp"
#include "server/api/config_types.hpp"
#include "server/api/mcp_types.hpp"
#include "server/api/session_types.hpp"
#include "server/api/workspace_types.hpp"
#include "server/api/file_types.hpp"
#include "server/core/router.hpp"
#include "server/session/pool.hpp"
#include "workspace/history_db.hpp"
#include "workspace/manager.hpp"
#include "agent/runtime/application/workspace_resolver.hpp"

namespace ben_gear::server::composition {

struct ApiServices {
    std::shared_ptr<SessionService> session;
    std::shared_ptr<ConfigService> config;
    std::shared_ptr<WorkspaceService> workspace;
    std::shared_ptr<McpService> mcp;
    std::shared_ptr<FileService> file;
};

struct ServerCompositionContext {
    config::Settings& settings;
    application::WorkspaceResolver& workspace_resolver;
    SessionPool& session_pool;
};

ApiServices make_api_services(ServerCompositionContext context);
void register_composed_api_routes(Router& router, ApiServices& services);

} // namespace ben_gear::server::composition
