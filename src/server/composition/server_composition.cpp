#include "server/composition/server_composition.hpp"

#include "server/api/handlers.hpp"
#include "server/composition/basic_api_composition.hpp"

namespace ben_gear::server::composition {

std::shared_ptr<IApiServiceRegistry> make_api_services(ServerCompositionContext context) {
    auto basic_ctx = BasicApiCompositionContext{
        context.settings, context.workspace_resolver, context.session_pool, context.history_db};
    auto registry = std::make_shared<ApiServiceRegistry>();
    registry->set_session(make_session_api_service(basic_ctx));
    registry->set_config(make_config_api_service(basic_ctx));
    registry->set_workspace(make_workspace_api_service(basic_ctx));
    registry->set_mcp(make_mcp_api_service());
    registry->set_file(make_file_api_service());
    return registry;
}

void register_composed_api_routes(Router& router, IApiServiceRegistry& services,
                                   std::shared_ptr<workspace::HistoryDB> history_db) {
    register_api_routes(router,
                        services.session_service(),
                        services.config_service(),
                        services.workspace_service(),
                        services.mcp_service(),
                        services.file_service(),
                        history_db);
}

} // namespace ben_gear::server::composition
