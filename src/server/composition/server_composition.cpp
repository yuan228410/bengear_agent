#include "server/composition/server_composition.hpp"

#include "server/api/handlers.hpp"

namespace ben_gear::server::composition {

ApiServices make_api_services(ServerCompositionContext) {
    return ApiServices{};
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
