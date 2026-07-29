#pragma once

#include <memory>

#include "config/settings.hpp"
#include "server/api/config_types.hpp"
#include "server/api/mcp_types.hpp"
#include "server/api/session_types.hpp"
#include "server/api/workspace_types.hpp"
#include "server/api/file_types.hpp"
#include "server/core/router.hpp"
#include "server/session/pool.hpp"
#include "workspace/history_db.hpp"
#include "workspace/manager.hpp"
#include "workspace/resolver.hpp"
#include "server/composition/api_service_registry.hpp"

namespace ben_gear::server::composition {

/// 向后兼容别名
using ApiServices = ApiServiceRegistry;

struct ServerCompositionContext {
    config::Settings& settings;
    workspace::WorkspaceResolver& workspace_resolver;
    SessionPool& session_pool;
    std::shared_ptr<workspace::HistoryDB> history_db;
};

std::shared_ptr<IApiServiceRegistry> make_api_services(ServerCompositionContext context);
void register_composed_api_routes(Router& router, IApiServiceRegistry& services,
                                   std::shared_ptr<workspace::HistoryDB> history_db,
                                   const workspace::WorkspaceResolver& resolver,
                                   SessionPool& session_pool);

} // namespace ben_gear::server::composition
