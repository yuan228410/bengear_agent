#include "server/composition/server_composition.hpp"

#include "server/api/handlers.hpp"
#include "server/composition/basic_api_composition.hpp"

namespace ben_gear::server::composition {

ApiServices make_api_services(ServerCompositionContext context) {
    auto basic_ctx = BasicApiCompositionContext{
        context.settings, context.workspace_resolver, context.session_pool, context.history_db};
    ApiServices services;
    services.session = make_session_api_service(basic_ctx);
    services.config = make_config_api_service(basic_ctx);
    services.workspace = make_workspace_api_service(basic_ctx);
    services.mcp = make_mcp_api_service();
    services.file = make_file_api_service();
    return services;
}

void register_composed_api_routes(Router& router, ApiServices& services) {
    register_api_routes(router,
                        services.session,
                        services.config,
                        services.workspace,
                        services.mcp,
                        services.file);
}

} // namespace ben_gear::server::composition
