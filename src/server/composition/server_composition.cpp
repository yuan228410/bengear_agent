#include "ben_gear/server/composition/server_composition.hpp"

#include "ben_gear/server/api/handlers.hpp"

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
                        services.file,
                        services.git,
                        services.permission,
                        services.patch,
                        services.checkpoint,
                        services.test_loop,
                        services.diagnostic_context,
                        services.diagnostic_repair,
                        services.repo_map,
                        services.code_intel,
                        services.audit);
}

} // namespace ben_gear::server::composition
